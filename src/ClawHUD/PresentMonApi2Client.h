#pragma once

#include "PresentMonApi2Api.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <map>

namespace clawhud
{
enum class PresentMonApi2InitFailure
{
    None,
    LoaderNotFound,
    LoaderLoadFailed,
    MissingEndpoint,
    VersionQueryFailed,
};

struct PresentMonApi2InitStatus
{
    PresentMonApi2InitFailure failure{ PresentMonApi2InitFailure::None };
    DWORD win32Error{};
    PM_STATUS apiStatus{ PM_STATUS_SUCCESS };
    const char* missingEndpoint{};
};

const char* PresentMonApi2InitFailureName(PresentMonApi2InitFailure failure) noexcept;
std::filesystem::path PresentMonApi2AppLocalLoaderPath(
    const std::filesystem::path& modulePath);

// Decides when the pmStartTrackingProcess / pmStopTrackingProcess endpoint must
// actually fire as independent production consumers (FPS telemetry, game-render
// frame verification) share the same target PID.
class ProcessTrackingRefCounts
{
public:
    // True when this is the first holder for `processId` (fire the start endpoint).
    bool Acquire(std::uint32_t processId);
    // Undo an Acquire whose endpoint start then failed.
    void AbortAcquire(std::uint32_t processId);
    // True when the last holder released `processId` (fire the stop endpoint).
    bool Release(std::uint32_t processId);
    unsigned Count(std::uint32_t processId) const;
    void Clear() noexcept { counts_.clear(); }

private:
    std::map<std::uint32_t, unsigned> counts_;
};

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
    const PresentMonApi2InitStatus& InitStatus() const noexcept { return initStatus_; }

    PM_STATUS OpenSession();
    void CloseSession() noexcept;
    bool SessionOpen() const noexcept { return session_ != nullptr; }

    // Reference counted per PID: multiple production consumers (FPS telemetry,
    // game-render frame verification) can independently hold the same target
    // without one releasing it while another still needs it. The underlying
    // pmStartTrackingProcess / pmStopTrackingProcess endpoint fires only on the
    // 0->1 and 1->0 transitions.
    virtual PM_STATUS StartTrackingProcess(std::uint32_t processId);
    virtual PM_STATUS StopTrackingProcess(std::uint32_t processId);
    PM_STATUS GetIntrospectionRoot(const PM_INTROSPECTION_ROOT** root);
    PM_STATUS FreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* root);
    PM_STATUS SetTelemetryPollingPeriod(std::uint32_t reserved, std::uint32_t periodMs);
    PM_STATUS SetEtwFlushPeriod(std::uint32_t periodMs);
    PM_STATUS FlushFrames(std::uint32_t processId);

    virtual PM_STATUS RegisterDynamicQuery(PM_DYNAMIC_QUERY_HANDLE* query,
        PM_QUERY_ELEMENT* elements, std::uint64_t elementCount,
        double windowSizeMs, double metricOffsetMs);
    virtual PM_STATUS FreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query);
    virtual PM_STATUS PollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query,
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
    PresentMonApi2InitStatus initStatus_{};
    std::filesystem::path loaderPath_;
    DWORD loaderError_{};
    ProcessTrackingRefCounts trackingRefs_;
    bool initialized_{};
};
}
