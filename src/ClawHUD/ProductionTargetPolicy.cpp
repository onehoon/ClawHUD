#include "ProductionTargetPolicy.h"

#include <array>
#include <utility>

namespace
{
class ProcessHandle
{
public:
    explicit ProcessHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~ProcessHandle()
    {
        if (handle_ != nullptr)
            CloseHandle(handle_);
    }

    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_{};
};

std::wstring Basename(std::wstring_view path)
{
    const auto separator = path.find_last_of(L"\\/");
    return std::wstring(path.substr(
        separator == std::wstring_view::npos ? 0 : separator + 1));
}

void NormalizeAsciiLowercase(std::wstring& value) noexcept
{
    for (auto& character : value)
    {
        if (character >= L'A' && character <= L'Z')
            character = static_cast<wchar_t>(character - L'A' + L'a');
    }
}
}

namespace clawhud
{
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept
{
    constexpr std::array<std::wstring_view, 43> rejected{
        L"explorer.exe", L"searchhost.exe", L"shellexperiencehost.exe",
        L"startmenuexperiencehost.exe", L"applicationframehost.exe",
        L"steam.exe", L"steamwebhelper.exe", L"steamservice.exe",
        L"gamebar.exe",
        L"gamebarftserver.exe", L"xboxpcappft.exe", L"gamingservices.exe",
        L"gamingservicesui.exe", L"pickerhost.exe", L"mongmode.exe",
        L"textinputhost.exe", L"chrome.exe", L"msedge.exe",
        L"firefox.exe", L"brave.exe", L"opera.exe", L"vivaldi.exe",
        L"steaminputaddonforclaw.ui.exe",
        L"msi center m.exe", L"msi_center_m_launcher.exe",
        L"msi_center_m_server.exe", L"msi_center_m_server_controlmode.exe",
        L"command center.exe", L"gamebar_widget.exe", L"mcmosdinfo.exe",
        L"gameoverlayui.exe", L"steamerrorreporter.exe",
        L"steamerrorreporter64.exe",
        L"xboxpcapp.exe", L"xboxpctray.exe",
        L"xboxgamebarwidgets.exe", L"gamingservicesnet.exe",
        L"windowsterminal.exe", L"runtimebroker.exe", L"dllhost.exe",
        L"backgroundtaskhost.exe", L"werfault.exe", L"crashreportclient.exe"};
    for (const auto candidate : rejected)
        if (candidate == image)
            return true;
    return false;
}

bool IsEligibleProductionTargetImage(std::wstring_view image) noexcept
{
    try
    {
        auto basename = Basename(image);
        NormalizeAsciiLowercase(basename);
        return !basename.empty() && !IsRejectedProductionTargetImage(basename);
    }
    catch (...)
    {
        return false;
    }
}

ProductionTargetInspection InspectProductionTargetProcessDetailed(
    DWORD processId, DWORD ownProcessId) noexcept
{
    if (processId == 0 || processId == ownProcessId)
        return {};

    try
    {
        ProcessHandle process(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
        if (process.get() == nullptr)
            return {};

        std::wstring imagePath(32768, L'\0');
        DWORD length = static_cast<DWORD>(imagePath.size());
        if (!QueryFullProcessImageNameW(
                process.get(), 0, imagePath.data(), &length))
            return {};
        imagePath.resize(length);

        auto imageName = Basename(imagePath);
        NormalizeAsciiLowercase(imageName);
        if (imageName.empty()) return {};
        if (IsRejectedProductionTargetImage(imageName))
            return {ProductionTargetInspectionStatus::Excluded,
                ProductionTargetProcess{processId, std::move(imageName)}};

        return {ProductionTargetInspectionStatus::Eligible,
            ProductionTargetProcess{processId, std::move(imageName)}};
    }
    catch (...)
    {
        return {};
    }
}

bool ShouldReevaluateForegroundAfterResume(bool hudEnabled,
    bool recoveryCompleted) noexcept
{
    return hudEnabled && recoveryCompleted;
}
}
