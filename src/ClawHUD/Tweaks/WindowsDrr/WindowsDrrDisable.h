#pragma once

#include <windows.h>

#include <cstdint>

namespace clawhud
{
enum class DrrDisableStatus
{
    AlreadyOff,
    Applied,
    Unsupported,
    Unavailable,
    ValidateFailed,
    ApplyFailed,
    VerifyFailed,
};

struct DrrDisableResult
{
    DrrDisableStatus status{ DrrDisableStatus::Unavailable };
    unsigned activePathCount{};
    unsigned changedPathCount{};
    LONG win32Error{};
};

class IWindowsDrrDisplayConfigApi
{
public:
    virtual ~IWindowsDrrDisplayConfigApi() = default;

    virtual LONG GetBufferSizes(UINT32 flags, UINT32* pathCount, UINT32* modeCount) = 0;
    virtual LONG Query(UINT32 flags, UINT32* pathCount,
        DISPLAYCONFIG_PATH_INFO* paths, UINT32* modeCount,
        DISPLAYCONFIG_MODE_INFO* modes) = 0;
    virtual LONG Set(UINT32 pathCount, DISPLAYCONFIG_PATH_INFO* paths,
        UINT32 modeCount, DISPLAYCONFIG_MODE_INFO* modes, UINT32 flags) = 0;
};

DrrDisableResult DisableWindowsDrrForActivePaths();
DrrDisableResult DisableWindowsDrrForActivePaths(IWindowsDrrDisplayConfigApi& api);
const wchar_t* DrrDisableStatusName(DrrDisableStatus status) noexcept;
}
