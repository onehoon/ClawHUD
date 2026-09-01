#include "GameProcessInstance.h"

namespace clawhud
{
std::optional<GameProcessInstance> QueryGameProcessInstance(
    DWORD processId) noexcept
{
    if (!processId)
        return std::nullopt;

    const HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return std::nullopt;

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    const bool queried = GetProcessTimes(
        process, &creation, &exit, &kernel, &user) != FALSE;
    CloseHandle(process);
    if (!queried)
        return std::nullopt;

    ULARGE_INTEGER creationValue{};
    creationValue.LowPart = creation.dwLowDateTime;
    creationValue.HighPart = creation.dwHighDateTime;
    return GameProcessInstance{processId, creationValue.QuadPart};
}
}
