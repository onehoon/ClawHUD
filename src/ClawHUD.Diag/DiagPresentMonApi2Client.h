#pragma once

// Diagnostic-local adaptation of the app-local API2 loader.  It deliberately
// keeps the same DLL contract while avoiding a build dependency on production.
#include "PresentMonApi2Api.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>

class DiagPresentMonApi2Client
{
public:
    ~DiagPresentMonApi2Client();
    bool Initialize() noexcept;
    void Shutdown() noexcept;
    PM_STATUS OpenSession() noexcept;
    PM_STATUS StartTrackingProcess(std::uint32_t processId) noexcept;
    PM_STATUS StopTrackingProcess(std::uint32_t processId) noexcept;
    PM_STATUS GetIntrospectionRoot(const PM_INTROSPECTION_ROOT** root) noexcept;
    PM_STATUS FreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* root) noexcept;
    PM_STATUS RegisterDynamicQuery(PM_DYNAMIC_QUERY_HANDLE* query, PM_QUERY_ELEMENT* elements,
        std::uint64_t elementCount, double windowSizeMs, double metricOffsetMs) noexcept;
    PM_STATUS FreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query) noexcept;
    PM_STATUS PollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query, std::uint32_t processId,
        std::uint8_t* blob, std::uint32_t* swapChainCount) noexcept;
    const PM_VERSION& ApiVersion() const noexcept { return version_; }

private:
    struct Endpoints;
    Endpoints* endpoints_{};
    HMODULE loader_{};
    PM_SESSION_HANDLE session_{};
    PM_VERSION version_{};
};
