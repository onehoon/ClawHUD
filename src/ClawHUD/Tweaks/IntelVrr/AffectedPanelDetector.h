#pragma once

#include "DisplayPathProbe.h"

#include <optional>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace clawhud
{
using IntelVrrWindowsTarget = VrrWindowsTarget;
using IntelVrrWindowsTargets = VrrWindowsTargets;

struct PanelIdentity
{
    std::string manufacturer;
    std::string productCode;
    std::string panelName;
    bool active{};
    std::string instanceName;
};

enum class SupportedPanelKind
{
    ClawInternal,
    Lg27gs65f,
};

enum class PanelTargetMappingStatus
{
    Exact,
    NotActive,
    Ambiguous,
    Unavailable,
};

struct PanelTargetMatch
{
    PanelTargetMappingStatus status{ PanelTargetMappingStatus::Unavailable };
    std::optional<std::size_t> targetIndex;
};

std::optional<SupportedPanelKind> ClassifySupportedPanel(const PanelIdentity& identity);
PanelTargetMatch ResolvePanelWindowsTarget(const PanelIdentity& panel,
    const IntelVrrWindowsTargets& windowsTargets);
std::wstring NormalizeMonitorInstancePath(std::wstring_view value);

bool IsAffectedPanel(const PanelIdentity& identity);
bool IsLg27gs65fPanel(const PanelIdentity& identity);
std::vector<PanelIdentity> EnumeratePanelIdentities();
std::string DecodeWmiUShortString(const std::vector<unsigned short>& values);
}
