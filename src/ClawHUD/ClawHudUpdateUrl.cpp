#include "ClawHudUpdateUrl.h"

namespace clawhud
{
namespace
{
constexpr std::string_view kReleaseBase =
    "https://github.com/onehoon/ClawHUD/releases";

std::wstring Widen(std::string_view ascii)
{
    return std::wstring(ascii.begin(), ascii.end());
}
}

bool IsSupportedReleaseFeedName(std::string_view name) noexcept
{
    return name == kStableReleaseFeedName;
}

bool IsSimpleVersionToken(std::string_view value) noexcept
{
    if (value.empty() || value.size() > 64)
        return false;
    if (value.front() == '.' || value.front() == '-' || value.back() == '.' ||
        value.back() == '-')
        return false;
    if (value.find("..") != std::string_view::npos)
        return false;
    bool sawDigit = false;
    bool inSuffix = false;
    for (const char c : value)
    {
        if (c == '-' && !inSuffix)
        {
            inSuffix = true;
            continue;
        }
        const bool ok = (c >= '0' && c <= '9') || c == '.' ||
            (inSuffix && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')));
        if (!ok)
            return false;
        if (c >= '0' && c <= '9')
            sawDigit = true;
    }
    return sawDigit;
}

bool IsPlainFileName(std::string_view value) noexcept
{
    if (value.empty() || value.size() > 256 || value == "." || value == "..")
        return false;
    if (value.find('/') != std::string_view::npos ||
        value.find('\\') != std::string_view::npos ||
        value.find("..") != std::string_view::npos)
        return false;
    return true;
}

std::optional<std::wstring> BuildReleaseFeedUrl(std::string_view releasesName)
{
    if (!IsSupportedReleaseFeedName(releasesName))
        return std::nullopt;
    return Widen(kReleaseBase) + L"/latest/download/" + Widen(releasesName);
}

std::optional<std::wstring> BuildPackageUrl(std::string_view assetVersion,
    std::string_view assetFileName)
{
    if (!IsSimpleVersionToken(assetVersion) || !IsPlainFileName(assetFileName))
        return std::nullopt;
    return Widen(kReleaseBase) + L"/download/v" + Widen(assetVersion) + L"/" +
        Widen(assetFileName);
}
}
