#include "ProductionTargetPolicy.h"

#include <array>

namespace clawhud
{
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept
{
    constexpr std::array<std::wstring_view, 13> rejected{
        L"explorer.exe", L"searchhost.exe", L"shellexperiencehost.exe",
        L"startmenuexperiencehost.exe", L"applicationframehost.exe",
        L"steam.exe", L"textinputhost.exe", L"chrome.exe", L"msedge.exe",
        L"firefox.exe", L"brave.exe", L"opera.exe", L"vivaldi.exe"};
    for (const auto candidate : rejected)
        if (candidate == image)
            return true;
    return false;
}
}
