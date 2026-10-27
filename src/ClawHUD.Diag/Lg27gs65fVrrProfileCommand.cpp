#include "Lg27gs65fVrrProfileCommand.h"

#include "DisplayPathProbe.h"

#include <windows.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace
{
constexpr std::uint32_t kRecommended = 1;
constexpr std::uint32_t kExcellent = 2;
constexpr float kToleranceHz = 0.25f;

wchar_t Fold(wchar_t value) noexcept
{
    return value >= L'a' && value <= L'z'
        ? static_cast<wchar_t>(value - (L'a' - L'A')) : value;
}

bool Contains(std::wstring_view value, std::wstring_view part) noexcept
{
    if (part.empty() || part.size() > value.size()) return false;
    for (std::size_t i = 0; i <= value.size() - part.size(); ++i)
    {
        std::size_t j{};
        while (j < part.size() && Fold(value[i + j]) == Fold(part[j])) ++j;
        if (j == part.size()) return true;
    }
    return false;
}

bool Equal(std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (Fold(left[i]) != Fold(right[i])) return false;
    return true;
}

bool Near(float value, float expected) noexcept
{
    return std::isfinite(value) && std::abs(value - expected) <= kToleranceHz;
}

std::wstring ManufacturerCode(std::wstring_view devicePath)
{
    const auto marker = devicePath.find(L'#');
    if (marker == std::wstring_view::npos || marker + 4 > devicePath.size() ||
        !Contains(devicePath.substr(0, marker), L"DISPLAY")) return {};
    const auto end = devicePath.find(L'#', marker + 1);
    const auto id = devicePath.substr(marker + 1,
        (end == std::wstring_view::npos ? devicePath.size() : end) - marker - 1);
    return id.size() >= 3 ? std::wstring(id.substr(0, 3)) : std::wstring{};
}

struct MonitorList
{
    HMONITOR handle{};
    std::uint32_t count{};
};

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
    auto& list = *reinterpret_cast<MonitorList*>(parameter);
    list.handle = monitor;
    ++list.count;
    return TRUE;
}

struct LgTarget
{
    HMONITOR monitor{};
    std::wstring sourceName;
    std::wstring friendlyName;
    std::wstring devicePath;
    std::wstring manufacturerCode;
    LUID adapterLuid{};
    UINT32 targetId{};
    std::uint32_t activePathCount{};
    std::uint32_t matchingMonitorCount{};
    bool identityVerified{};
};

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

std::optional<LgTarget> FindSingleActiveTarget(std::string& error)
{
    MonitorList monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
            reinterpret_cast<LPARAM>(&monitors)) || monitors.count != 1)
    {
        error = "requires exactly one desktop monitor";
        return std::nullopt;
    }
    const auto mapped = DisplayPathProbe{}.Probe(monitors.handle);
    if (mapped.status != DisplayPathProbeStatus::Matched || !mapped.path ||
        mapped.path->activePathCount != 1)
    {
        error = "requires one unambiguous active Windows target path";
        return std::nullopt;
    }
    DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
    name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    name.header.size = sizeof(name);
    name.header.adapterId = mapped.path->targetAdapterLuid;
    name.header.id = mapped.path->targetId;
    if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS)
    {
        error = "could not read the active monitor identity";
        return std::nullopt;
    }
    LgTarget target;
    target.sourceName = mapped.path->monitorDeviceName;
    target.friendlyName = name.monitorFriendlyDeviceName;
    target.devicePath = name.monitorDevicePath;
    target.manufacturerCode = ManufacturerCode(target.devicePath);
    target.adapterLuid = mapped.path->targetAdapterLuid;
    target.targetId = mapped.path->targetId;
    target.activePathCount = mapped.path->activePathCount;
    target.matchingMonitorCount = monitors.count;
    target.identityVerified = IsLg27gs65fIdentity(target.manufacturerCode,
        target.friendlyName);
    return target;
}

std::string Utf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "<unavailable>";
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::string LuidText(const LUID& luid)
{
    std::ostringstream value;
    value << "0x" << std::hex << std::uppercase << std::setfill('0')
          << std::setw(8) << static_cast<std::uint32_t>(luid.HighPart) << ':'
          << std::setw(8) << luid.LowPart;
    return value.str();
}

const char* ProfileName(std::uint32_t profile) noexcept
{
    switch (profile)
    {
    case 1: return "RECOMMENDED";
    case 2: return "EXCELLENT";
    case 3: return "GOOD";
    case 4: return "COMPATIBLE";
    case 5: return "OFF";
    case 6: return "VESA";
    case 7: return "CUSTOM";
    default: return "INVALID/UNKNOWN";
    }
}

void PrintTarget(const LgTarget& target)
{
    std::cout << "Active monitor: " << Utf8(target.friendlyName) << '\n'
              << "Monitor identity: manufacturer=" << Utf8(target.manufacturerCode)
              << ", device path=" << Utf8(target.devicePath) << '\n'
              << "Windows target: " << LuidText(target.adapterLuid) << " / "
              << target.targetId << " (" << Utf8(target.sourceName) << ")\n";
}

void PrintState(const DiagIntelVrrState& state)
{
    std::cout << "IGCL mapping: "
              << (state.mappingStatus == DiagIgclTargetMappingStatus::Exact ? "EXACT" :
                  state.mappingStatus == DiagIgclTargetMappingStatus::Ambiguous ? "AMBIGUOUS" : "UNKNOWN")
              << ", enumeration=" << (state.enumerationComplete ? "complete" : "incomplete")
              << ", exact matches=" << state.targetMatchCount
              << '\n';
    if (state.capability)
        std::cout << "Capability range: " << state.capability->minimumHz << "-"
                  << state.capability->maximumHz << " Hz, supported="
                  << (state.capability->supported ? "YES" : "NO") << '\n';
    else std::cout << "Capability range: unavailable\n";
    if (state.profile)
        std::cout << "Current profile: " << ProfileName(state.profile->profile)
                  << " (" << state.profile->minimumHz << "-"
                  << state.profile->maximumHz << " Hz)\n";
    else std::cout << "Current profile: unavailable\n";
    if (state.capabilityResult)
        std::cout << "Capability API result: 0x" << std::hex << std::uppercase
                  << *state.capabilityResult << std::dec << '\n';
    if (state.profileResult)
        std::cout << "Profile API result: 0x" << std::hex << std::uppercase
                  << *state.profileResult << std::dec << '\n';
}

bool Confirm(std::string_view prompt)
{
    std::cout << prompt << " [y/N]: " << std::flush;
    std::string input;
    if (!std::getline(std::cin, input)) return false;
    return IsLg27gs65fConfirmationAccepted(input);
}

bool SameTarget(const LgTarget& left, const LgTarget& right) noexcept
{
    return left.identityVerified && right.identityVerified &&
        SameLuid(left.adapterLuid, right.adapterLuid) &&
        left.targetId == right.targetId &&
        Equal(left.sourceName, right.sourceName) &&
        Equal(left.friendlyName, right.friendlyName) &&
        Equal(left.devicePath, right.devicePath);
}

void PrintSetResult(const DiagIgclProfileSetResult& result)
{
    if (result.apiResult)
        std::cout << "IGCL API result: 0x" << std::hex << std::uppercase
                  << *result.apiResult << std::dec << '\n';
    if (!result.detail.empty()) std::cout << "IGCL detail: " << result.detail << '\n';
}

void RunAction(DiagIntelVrrStateProbe& probe, const LgTarget& original,
    const DiagIntelVrrState& before, Lg27gs65fVrrAction action)
{
    const bool applying = action == Lg27gs65fVrrAction::ApplyExcellent;
    const auto actionLabel = applying ? "Apply" : "Restore";
    const auto initialDecision = EvaluateLg27gs65fAction(action,
        original.identityVerified, original.activePathCount,
        original.matchingMonitorCount, false, before);
    if (initialDecision == Lg27gs65fVrrDecision::AlreadyAtTarget)
    {
        std::cout << "Already at the requested profile; no write performed.\n";
        return;
    }
    if (initialDecision != Lg27gs65fVrrDecision::ConfirmationRequired)
    {
        std::cout << actionLabel << " result: SKIPPED (identity, mapping, capability, or profile state is not eligible).\n";
        return;
    }

    if (!Confirm(applying ? "Apply EXCELLENT to this exact LG target?" :
                            "Restore RECOMMENDED on this exact LG target?"))
    {
        std::cout << actionLabel << " result: SKIPPED (cancelled).\n";
        return;
    }

    std::string error;
    const auto confirmedTarget = FindSingleActiveTarget(error);
    if (!confirmedTarget || !SameTarget(original, *confirmedTarget))
    {
        std::cout << actionLabel << " result: SKIPPED (display identity/topology changed; "
                  << (error.empty() ? "target differs" : error) << ").\n";
        return;
    }
    const auto confirmedState = probe.Query(confirmedTarget->adapterLuid,
        confirmedTarget->targetId);
    if (EvaluateLg27gs65fAction(action, confirmedTarget->identityVerified,
            confirmedTarget->activePathCount, confirmedTarget->matchingMonitorCount,
            true, confirmedState) != Lg27gs65fVrrDecision::Write)
    {
        std::cout << actionLabel << " result: SKIPPED (IGCL state changed after confirmation).\n";
        PrintState(confirmedState);
        return;
    }

    error.clear();
    const auto immediateTarget = FindSingleActiveTarget(error);
    if (!immediateTarget || !SameTarget(*confirmedTarget, *immediateTarget))
    {
        std::cout << actionLabel << " result: SKIPPED (display identity/topology changed before write).\n";
        return;
    }
    const auto requested = applying ? kExcellent : kRecommended;
    const auto set = probe.SetProfileForExactTarget(confirmedState, requested);
    PrintSetResult(set);
    if (!set.attempted)
    {
        std::cout << actionLabel << " result: SKIPPED (" << set.detail << ").\n";
        return;
    }
    if (!set.succeeded)
    {
        std::cout << actionLabel << " result: FAILED (profile setter returned an error).\n";
        return;
    }

    error.clear();
    const auto readbackTarget = FindSingleActiveTarget(error);
    if (!readbackTarget || !SameTarget(*confirmedTarget, *readbackTarget))
    {
        std::cout << actionLabel << " result: VERIFICATION FAILED (target changed; no rollback write).\n";
        return;
    }
    const auto readback = probe.Query(readbackTarget->adapterLuid,
        readbackTarget->targetId);
    std::cout << "After:\n";
    PrintState(readback);
    if (VerifyLg27gs65fAction(action, set.succeeded, true, readback))
    {
        std::cout << actionLabel << " result: SUCCESS\n";
        return;
    }

    std::cout << actionLabel << " result: VERIFICATION FAILED\n";
    if (applying && readback.mappingStatus == DiagIgclTargetMappingStatus::Exact &&
        readback.profile && readback.profile->profile == kExcellent)
    {
        error.clear();
        const auto rollbackTarget = FindSingleActiveTarget(error);
        if (!rollbackTarget || !SameTarget(*readbackTarget, *rollbackTarget))
        {
            std::cout << "Rollback: SKIPPED (display identity/topology changed).\n";
            return;
        }
        const auto rollback = probe.SetProfileForExactTarget(readback, kRecommended);
        PrintSetResult(rollback);
        if (!rollback.succeeded)
        {
            std::cout << "Rollback: " << (rollback.attempted ? "FAILED" : "SKIPPED") << '\n';
            return;
        }

        error.clear();
        const auto restoredTarget = FindSingleActiveTarget(error);
        if (!restoredTarget || !SameTarget(*readbackTarget, *restoredTarget))
        {
            std::cout << "Rollback: VERIFICATION FAILED (target changed)\n";
            return;
        }
        const auto restored = probe.Query(restoredTarget->adapterLuid,
            restoredTarget->targetId);
        const bool rollbackVerified = VerifyLg27gs65fAction(
            Lg27gs65fVrrAction::RestoreRecommended, true, true, restored);
        std::cout << "Rollback: " << (rollbackVerified ? "SUCCESS" : "VERIFICATION FAILED") << '\n';
        PrintState(restored);
    }
}
}

bool IsLg27gs65fIdentity(std::wstring_view manufacturerCode,
    std::wstring_view friendlyName) noexcept
{
    return manufacturerCode.size() == 3 && Fold(manufacturerCode[0]) == L'G' &&
        Fold(manufacturerCode[1]) == L'S' && Fold(manufacturerCode[2]) == L'M' &&
        (Contains(friendlyName, L"27GS65F") || Contains(friendlyName, L"LG ULTRAGEAR"));
}

bool IsLg27gs65fConfirmationAccepted(std::string_view input) noexcept
{
    return input == "y" || input == "Y";
}

int ParseLg27gs65fMenuSelection(std::string_view input) noexcept
{
    return input == "1" ? 1 : input == "2" ? 2 : input == "3" ? 3 : 0;
}

Lg27gs65fVrrDecision EvaluateLg27gs65fAction(Lg27gs65fVrrAction action,
    bool identityVerified, std::uint32_t activePathCount,
    std::uint32_t matchingMonitorCount, bool confirmed,
    const DiagIntelVrrState& state) noexcept
{
    if (!identityVerified || activePathCount != 1 || matchingMonitorCount != 1 ||
        !state.initialized || !state.enumerationComplete ||
        state.mappingStatus != DiagIgclTargetMappingStatus::Exact ||
        state.targetMatchCount != 1 || state.displayOutputCount == 0 ||
        state.displayPropertiesSuccessCount != state.displayOutputCount ||
        !state.capability || !state.profile || state.capabilityResult != 0 ||
        state.profileResult != 0 || !state.capability->supported ||
        !Near(state.capability->minimumHz, 48.0f) ||
        !Near(state.capability->maximumHz, 180.0f))
        return Lg27gs65fVrrDecision::Skip;

    const auto& profile = *state.profile;
    if (action == Lg27gs65fVrrAction::ApplyExcellent &&
        profile.profile == kExcellent && Near(profile.minimumHz, 48.0f) &&
        Near(profile.maximumHz, 180.0f))
        return Lg27gs65fVrrDecision::AlreadyAtTarget;
    if (action == Lg27gs65fVrrAction::RestoreRecommended &&
        profile.profile == kRecommended)
        return Lg27gs65fVrrDecision::AlreadyAtTarget;

    const bool validActiveRange = std::isfinite(profile.minimumHz) &&
        std::isfinite(profile.maximumHz) && profile.minimumHz > 0.0f &&
        profile.maximumHz >= profile.minimumHz &&
        profile.minimumHz >= state.capability->minimumHz - kToleranceHz &&
        profile.maximumHz <= state.capability->maximumHz + kToleranceHz;
    const bool eligible = action == Lg27gs65fVrrAction::ApplyExcellent
        ? profile.profile == kRecommended && Near(profile.minimumHz, 90.0f) &&
            Near(profile.maximumHz, 180.0f)
        : profile.profile == kExcellent && validActiveRange;
    if (!eligible) return Lg27gs65fVrrDecision::Skip;
    return confirmed ? Lg27gs65fVrrDecision::Write :
        Lg27gs65fVrrDecision::ConfirmationRequired;
}

bool VerifyLg27gs65fAction(Lg27gs65fVrrAction action, bool setterSucceeded,
    bool sameTarget, const DiagIntelVrrState& readback) noexcept
{
    return setterSucceeded && sameTarget &&
        EvaluateLg27gs65fAction(action, true, 1, 1, true, readback) ==
            Lg27gs65fVrrDecision::AlreadyAtTarget;
}

void RunLg27gs65fVrrProfileCommand()
{
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    DiagIntelVrrStateProbe probe;
    if (!probe.Initialize())
    {
        std::cout << "LG VRR Profile Test unavailable: IGCL initialization failed.\n";
        return;
    }

    for (;;)
    {
        std::string error;
        const auto target = FindSingleActiveTarget(error);
        if (!target)
        {
            std::cout << "LG VRR Profile Test skipped: " << error << ". No change made.\n";
            return;
        }
        std::cout << "\n=== LG 27GS65F VRR Profile Test ===\n";
        PrintTarget(*target);
        if (!target->identityVerified)
        {
            std::cout << "Identity not verified; no change made.\n";
            return;
        }
        const auto state = probe.Query(target->adapterLuid, target->targetId);
        std::cout << "Before:\n";
        PrintState(state);
        const auto applyDecision = EvaluateLg27gs65fAction(
            Lg27gs65fVrrAction::ApplyExcellent, true,
            target->activePathCount, target->matchingMonitorCount, false, state);
        const auto restoreDecision = EvaluateLg27gs65fAction(
            Lg27gs65fVrrAction::RestoreRecommended, true,
            target->activePathCount, target->matchingMonitorCount, false, state);
        if (applyDecision == Lg27gs65fVrrDecision::Skip &&
            restoreDecision == Lg27gs65fVrrDecision::Skip)
        {
            std::cout << "Preflight failed; no change made.\n";
            return;
        }
        std::cout << "\n1. Apply EXCELLENT (try native 48-180 Hz)\n"
                  << "2. Restore RECOMMENDED\n3. Back\n\nSelect: ";
        std::string input;
        if (!std::getline(std::cin, input)) return;
        const int selection = ParseLg27gs65fMenuSelection(input);
        if (selection == 0)
        {
            std::cout << "Invalid input; no change made.\n";
            continue;
        }
        if (selection == 3) return;
        RunAction(probe, *target, state, selection == 1
            ? Lg27gs65fVrrAction::ApplyExcellent
            : Lg27gs65fVrrAction::RestoreRecommended);
    }
}
