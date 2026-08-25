#pragma once

#include <windows.h>

#include <optional>

namespace clawhud
{
struct WindowsPowerTelemetry
{
    std::optional<int> batteryPercent;
    std::optional<int> remainingMinutes;
    std::optional<bool> onBattery;
};

std::optional<WindowsPowerTelemetry> DecodeWindowsPowerStatus(
    const SYSTEM_POWER_STATUS& status);
std::optional<WindowsPowerTelemetry> ReadWindowsPowerTelemetry();
}
