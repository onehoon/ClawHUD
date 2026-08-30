#pragma once

#include <optional>

namespace clawhud
{
template<class T>
void UpdateRetainedTelemetryField(std::optional<T>& cached,
    const std::optional<T>& current, unsigned& missingCount,
    unsigned missingThreshold) noexcept
{
    if (current)
    {
        cached = current;
        missingCount = 0;
        return;
    }
    if (!cached)
        return;
    if (missingThreshold != 0 && ++missingCount >= missingThreshold)
    {
        cached.reset();
        missingCount = 0;
    }
}
}
