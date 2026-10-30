#include "Tweaks/IntelVrr/AffectedPanelDetector.h"

#include <iostream>
#include <vector>

using namespace clawhud;

namespace
{
bool Check(bool value, const char* message)
{
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}

PanelIdentity TmaPanel(bool active = true,
    const char* instance = "DISPLAY\\TMA2027\\5&3&UID3_0")
{
    return { "TMA", "2027", "TL070FVXS02-0", active, instance };
}

IntelVrrWindowsTarget TmaTarget(UINT32 targetId = 33)
{
    return { L"\\\\.\\DISPLAY3",
        L"\\\\?\\DISPLAY#TMA2027#5&3&UID3#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}",
        L"TL070FVXS02-0", LUID{ 303, 3 }, targetId, 3, true };
}
}

int main()
{
    bool ok = true;
    ok &= Check(ClassifySupportedPanel(TmaPanel()) == SupportedPanelKind::Claw7Internal,
        "the exact TMA/2027/TL070FVXS02-0 identity is classified as Claw7Internal");
    ok &= Check(IsAffectedPanel(TmaPanel(false)),
        "WMI Active is diagnostic metadata when classifying TMA2027");

    for (const auto& panel : std::vector<PanelIdentity>{
            { "TMA", "2027", "TL070FVXS02-1", true, "DISPLAY\\TMA2027\\x_0" },
            { "TMA", "2028", "TL070FVXS02-0", true, "DISPLAY\\TMA2027\\x_0" },
            { "ABC", "2027", "TL070FVXS02-0", true, "DISPLAY\\TMA2027\\x_0" } })
    {
        ok &= Check(!ClassifySupportedPanel(panel) && !IsAffectedPanel(panel),
            "TMA classification rejects any incorrect identity field");
    }
    ok &= Check(ClassifySupportedPanel(PanelIdentity{ "tma", "2027",
            "tl070fvxs02-0", true, "DISPLAY\\TMA2027\\5&3&UID3_0" }) ==
            SupportedPanelKind::Claw7Internal,
        "TMA identity comparison is case insensitive");

    const auto exact = IntelVrrWindowsTargets{ { TmaTarget() }, true, 0 };
    ok &= Check(ResolvePanelWindowsTarget(TmaPanel(false), exact).status ==
            PanelTargetMappingStatus::Exact,
        "exact active DISPLAY\\TMA2027 target is authoritative despite WMI Active=false");
    auto wrongInstance = TmaTarget();
    wrongInstance.monitorDevicePath =
        L"\\\\?\\DISPLAY#TMA2027#5&4&UID4#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}";
    ok &= Check(ResolvePanelWindowsTarget(TmaPanel(),
            IntelVrrWindowsTargets{ { wrongInstance }, true, 0 }).status ==
            PanelTargetMappingStatus::NotActive,
        "same-model different monitor instance never receives the target");
    auto wrongFamily = TmaTarget();
    wrongFamily.monitorDevicePath =
        L"\\\\?\\DISPLAY#TMA2028#5&3&UID3#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}";
    ok &= Check(ResolvePanelWindowsTarget(TmaPanel(),
            IntelVrrWindowsTargets{ { wrongFamily }, true, 0 }).status ==
            PanelTargetMappingStatus::NotActive,
        "TMA panel cannot bind to a non-TMA2027 Windows path");
    ok &= Check(ResolvePanelWindowsTarget(TmaPanel(),
            IntelVrrWindowsTargets{ { TmaTarget(), TmaTarget() }, true, 0 }).status ==
            PanelTargetMappingStatus::Ambiguous,
        "duplicate exact active Windows targets remain ambiguous");
    ok &= Check(ResolvePanelWindowsTarget(TmaPanel(),
            IntelVrrWindowsTargets{ { TmaTarget() }, false, 0 }).status ==
            PanelTargetMappingStatus::Unavailable,
        "incomplete active-path enumeration fails closed");

    return ok ? 0 : 1;
}
