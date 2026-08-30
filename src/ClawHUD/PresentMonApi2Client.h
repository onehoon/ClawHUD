#pragma once

#include "PresentMonApi2Api.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>

namespace clawhud
{
std::filesystem::path PresentMonApi2AppLocalLoaderPath(
    const std::filesystem::path& modulePath);

class PresentMonApi2Client
{
public:
    PresentMonApi2Client() = default;
    ~PresentMonApi2Client();

    PresentMonApi2Client(const PresentMonApi2Client&) = delete;
    PresentMonApi2Client& operator=(const PresentMonApi2Client&) = delete;

    bool Initialize();
    void Shutdown() noexcept;
    bool Initialized() const noexcept { return initialized_; }

    const std::filesystem::path& LoaderPath() const noexcept { return loaderPath_; }
    DWORD LoaderError() const noexcept { return loaderError_; }
    const PM_VERSION& ApiVersion() const noexcept { return version_; }

    PM_STATUS OpenSession();
    void CloseSession() noexcept;
    bool SessionOpen() const noexcept { return session_ != nullptr; }

    PM_STATUS StartTrackingProcess(std::uint32_t processId);
    PM_STATUS StopTrackingProcess(std::uint32_t processId);
    PM_STATUS GetIntrospectionRoot(const PM_INTROSPECTION_ROOT** root);
    PM_STATUS FreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* root);
    PM_STATUS SetTelemetryPollingPeriod(std::uint32_t reserved, std::uint32_t periodMs);
    PM_STATUS SetEtwFlushPeriod(std::uint32_t periodMs);
    PM_STATUS FlushFrames(std::uint32_t processId);

    PM_STATUS RegisterDynamicQuery(PM_DYNAMIC_QUERY_HANDLE* query,
        PM_QUERY_ELEMENT* elements, std::uint64_t elementCount,
        double windowSizeMs, double metricOffsetMs);
    PM_STATUS FreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query);
    PM_STATUS PollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query,
        std::uint32_t processId, std::uint8_t* blob, std::uint32_t* swapChainCount);
    PM_STATUS PollStaticQuery(const PM_QUERY_ELEMENT* element,
        std::uint32_t processId, std::uint8_t* blob);
    PM_STATUS RegisterFrameQuery(PM_FRAME_QUERY_HANDLE* query,
        PM_QUERY_ELEMENT* elements, std::uint64_t elementCount,
        std::uint32_t* blobSize);
    PM_STATUS ConsumeFrames(PM_FRAME_QUERY_HANDLE query,
        std::uint32_t processId, std::uint8_t* blob, std::uint32_t* frameCount);
    PM_STATUS FreeFrameQuery(PM_FRAME_QUERY_HANDLE query);

private:
    struct Endpoints;
    Endpoints* endpoints_{};
    HMODULE loader_{};
    PM_SESSION_HANDLE session_{};
    PM_VERSION version_{};
    std::filesystem::path loaderPath_;
    DWORD loaderError_{};
    bool initialized_{};
};
}
