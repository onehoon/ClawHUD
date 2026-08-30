#include "AlwaysModeFpsTarget.h"

namespace clawhud
{
bool AlwaysModeFpsTarget::SetForegroundProcess(DWORD processId) noexcept
{
    if (processId == targetProcessId_)
        return false;
    targetProcessId_ = processId;
    published_ = {};
    return true;
}

void AlwaysModeFpsTarget::AcceptSample(DWORD processId,
    std::optional<double> displayedFps) noexcept
{
    if (processId == 0 || processId != targetProcessId_)
        return;
    published_ = {processId, displayedFps};
}

std::optional<double> AlwaysModeFpsTarget::DisplayedFps() const noexcept
{
    if (targetProcessId_ == 0 || published_.processId != targetProcessId_)
        return std::nullopt;
    return published_.displayedFps;
}

void AlwaysModeFpsTarget::Release() noexcept
{
    targetProcessId_ = 0;
    published_ = {};
}
}
