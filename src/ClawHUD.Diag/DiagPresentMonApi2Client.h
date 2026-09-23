#pragma once

// Diagnostic-local adaptation of the app-local API2 loader.  It deliberately
// keeps the same DLL contract while avoiding a build dependency on production.
#include "PresentMonApi2Api.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>

class DiagPresentMonApi2ClientApi
{
public:
    virtual ~DiagPresentMonApi2ClientApi() = default;

    virtual bool Initialize() noexcept = 0;
    virtual void Shutdown() noexcept = 0;
    virtual PM_STATUS OpenSession() noexcept = 0;
    virtual PM_STATUS StartTrackingProcess(std::uint32_t processId) noexcept = 0;
    virtual PM_STATUS StopTrackingProcess(std::uint32_t processId) noexcept = 0;
    virtual PM_STATUS GetIntrospectionRoot(const PM_INTROSPECTION_ROOT** root) noexcept = 0;
    virtual PM_STATUS FreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* root) noexcept = 0;
    virtual bool FrameQueryEndpointsAvailable() const noexcept = 0;
    virtual PM_STATUS SetEtwFlushPeriod(std::uint32_t periodMs) noexcept = 0;
    virtual PM_STATUS FlushFrames(std::uint32_t processId) noexcept = 0;
    virtual PM_STATUS RegisterFrameQuery(PM_FRAME_QUERY_HANDLE* query,
        PM_QUERY_ELEMENT* elements, std::uint64_t elementCount,
        std::uint32_t* blobSize) noexcept = 0;
    virtual PM_STATUS ConsumeFrames(PM_FRAME_QUERY_HANDLE query, std::uint32_t processId,
        std::uint8_t* blob, std::uint32_t* frameCount) noexcept = 0;
    virtual PM_STATUS FreeFrameQuery(PM_FRAME_QUERY_HANDLE query) noexcept = 0;
    virtual const PM_VERSION& ApiVersion() const noexcept = 0;
};

class DiagPresentMonApi2Client : public DiagPresentMonApi2ClientApi
{
public:
    ~DiagPresentMonApi2Client() override;
    bool Initialize() noexcept override;
    void Shutdown() noexcept override;
    PM_STATUS OpenSession() noexcept override;
    PM_STATUS StartTrackingProcess(std::uint32_t processId) noexcept override;
    PM_STATUS StopTrackingProcess(std::uint32_t processId) noexcept override;
    PM_STATUS GetIntrospectionRoot(const PM_INTROSPECTION_ROOT** root) noexcept override;
    PM_STATUS FreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* root) noexcept override;
    PM_STATUS RegisterDynamicQuery(PM_DYNAMIC_QUERY_HANDLE* query, PM_QUERY_ELEMENT* elements,
        std::uint64_t elementCount, double windowSizeMs, double metricOffsetMs) noexcept;
    PM_STATUS FreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query) noexcept;
    PM_STATUS PollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query, std::uint32_t processId,
        std::uint8_t* blob, std::uint32_t* swapChainCount) noexcept;
    bool FrameQueryEndpointsAvailable() const noexcept override;
    PM_STATUS SetEtwFlushPeriod(std::uint32_t periodMs) noexcept override;
    PM_STATUS FlushFrames(std::uint32_t processId) noexcept override;
    PM_STATUS RegisterFrameQuery(PM_FRAME_QUERY_HANDLE* query, PM_QUERY_ELEMENT* elements,
        std::uint64_t elementCount, std::uint32_t* blobSize) noexcept override;
    PM_STATUS ConsumeFrames(PM_FRAME_QUERY_HANDLE query, std::uint32_t processId,
        std::uint8_t* blob, std::uint32_t* frameCount) noexcept override;
    PM_STATUS FreeFrameQuery(PM_FRAME_QUERY_HANDLE query) noexcept override;
    const PM_VERSION& ApiVersion() const noexcept override { return version_; }

private:
    struct Endpoints;
    Endpoints* endpoints_{};
    HMODULE loader_{};
    PM_SESSION_HANDLE session_{};
    PM_VERSION version_{};
};
