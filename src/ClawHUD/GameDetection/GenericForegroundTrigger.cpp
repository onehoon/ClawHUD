#include "GenericForegroundTrigger.h"

#include "../ProductionTargetPolicy.h"

#include <windows.h>

#include <string>

namespace clawhud
{
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
    return std::wstring(path.substr(separator == std::wstring_view::npos ? 0 : separator + 1));
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

bool IsGenericForegroundImageEligible(std::wstring_view image) noexcept
{
    std::wstring basename;
    try
    {
        basename = Basename(image);
        NormalizeAsciiLowercase(basename);
    }
    catch (...)
    {
        return false;
    }
    return !basename.empty() && !IsRejectedProductionTargetImage(basename);
}

std::optional<GenericForegroundEvidence> GenericForegroundTrigger::Inspect(
    HWND window, DWORD processId) const noexcept
{
    if (window == nullptr || processId == 0 || processId == GetCurrentProcessId())
        return std::nullopt;

    try
    {
        ProcessHandle process(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
        if (process.get() == nullptr)
            return std::nullopt;

        std::wstring imagePath(32768, L'\0');
        DWORD length = static_cast<DWORD>(imagePath.size());
        if (!QueryFullProcessImageNameW(process.get(), 0, imagePath.data(), &length))
            return std::nullopt;
        imagePath.resize(length);
        if (!IsGenericForegroundImageEligible(imagePath))
            return std::nullopt;

        return GenericForegroundEvidence{window, processId};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

GameDetectionTransitionResult GenericForegroundTrigger::ApplyEvidence(
    GameDetectionCoordinator& coordinator,
    const GenericForegroundEvidence& evidence) noexcept
{
    return coordinator.ObserveCandidate(
        evidence.processId, evidence.window,
        GameDetectionTrigger::GenericForeground);
}
}
