#pragma once

#include <windows.h>

#include <string_view>

namespace clawhud
{
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept;
bool ShouldRetainCommittedProductionTarget(DWORD processId, bool alive) noexcept;
bool ShouldConfirmProductionTarget(DWORD pendingProcessId, DWORD observedProcessId,
    bool displayedFpsAvailable) noexcept;
bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept;
}
