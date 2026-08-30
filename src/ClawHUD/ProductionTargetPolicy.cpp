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
CandidateDisposition DecideCandidateDisposition(
    const GameDetectionContext& context,
    GameDetectionTrigger incomingTrigger, DWORD incomingProcessId) noexcept
{
    if (incomingProcessId == 0)
        return CandidateDisposition::Ignore;
    if (context.candidateProcessId == incomingProcessId)
        return CandidateDisposition::Merge;
    if (context.state == GameDetectionState::Committed)
        return CandidateDisposition::Ignore;
    if (context.state == GameDetectionState::Ready)
        return CandidateDisposition::Ignore;
    if (context.candidateProcessId == 0)
        return CandidateDisposition::Replace;
    if (context.evidence.microsoftGameIdentity)
        return CandidateDisposition::Ignore;
    if (context.state == GameDetectionState::Verifying &&
        (incomingTrigger == GameDetectionTrigger::MicrosoftGameIdentity ||
            incomingTrigger == GameDetectionTrigger::GenericForeground))
        return CandidateDisposition::Replace;
    return CandidateDisposition::Ignore;
}

bool ShouldCommitReadyCandidate(
    const GameDetectionContext& context,
    DWORD foregroundProcessId, bool candidateProcessAlive) noexcept
{
    return context.state == GameDetectionState::Ready &&
        context.candidateProcessId != 0 &&
        context.candidateProcessId == foregroundProcessId &&
        candidateProcessAlive;
}

bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept
{
    constexpr std::array<std::wstring_view, 33> rejected{
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
        L"steamerrorreporter64.exe"};
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

std::optional<ProductionTargetProcess> InspectProductionTargetProcess(
    DWORD processId, DWORD ownProcessId) noexcept
{
    if (processId == 0 || processId == ownProcessId)
        return std::nullopt;

    try
    {
        ProcessHandle process(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
        if (process.get() == nullptr)
            return std::nullopt;

        std::wstring imagePath(32768, L'\0');
        DWORD length = static_cast<DWORD>(imagePath.size());
        if (!QueryFullProcessImageNameW(
                process.get(), 0, imagePath.data(), &length))
            return std::nullopt;
        imagePath.resize(length);

        auto imageName = Basename(imagePath);
        NormalizeAsciiLowercase(imageName);
        if (imageName.empty() || IsRejectedProductionTargetImage(imageName))
            return std::nullopt;

        return ProductionTargetProcess{processId, std::move(imageName)};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool ShouldRetainCommittedProductionTarget(DWORD committedProcessId,
    bool processAlive) noexcept
{
    return committedProcessId != 0 && processAlive;
}

bool ShouldRetryProductionPresentMon(DWORD retryProcessId,
    unsigned retryAttempts, DWORD processId) noexcept
{
    return processId != 0 && (retryProcessId != processId || retryAttempts == 0);
}

bool ShouldAllowProductionPresentMonStart(DWORD committedProcessId,
    DWORD processId, DWORD retryProcessId, unsigned retryAttempts,
    bool recoveryStart) noexcept
{
    return committedProcessId != processId || recoveryStart ||
        ShouldRetryProductionPresentMon(retryProcessId, retryAttempts, processId);
}

bool ShouldReevaluateForegroundAfterResume(bool hudEnabled,
    bool recoveryCompleted) noexcept
{
    return hudEnabled && recoveryCompleted;
}

bool ShouldReevaluateForegroundAfterDiagnostic(bool hudEnabled,
    bool diagnosticRunning, bool suspended) noexcept
{
    return hudEnabled && !diagnosticRunning && !suspended;
}

bool ShouldRestartGraphicsApiProbe(DWORD probedProcessId,
    DWORD committedProcessId) noexcept
{
    return committedProcessId != 0 && probedProcessId != committedProcessId;
}

bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept
{
    return hudEnabled && !diagnosticRunning && !suspended;
}
}
