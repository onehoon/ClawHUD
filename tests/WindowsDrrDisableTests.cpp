#include "Tweaks/WindowsDrr/WindowsDrrDisable.h"
#include "RuntimeLogger.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace clawhud;
namespace
{
constexpr UINT32 kExpectedReadFlags = QDC_ONLY_ACTIVE_PATHS |
    QDC_VIRTUAL_MODE_AWARE |
    QDC_VIRTUAL_REFRESH_RATE_AWARE;
constexpr UINT32 kExpectedSetBaseFlags = SDC_USE_SUPPLIED_DISPLAY_CONFIG |
    SDC_VIRTUAL_MODE_AWARE |
    SDC_VIRTUAL_REFRESH_RATE_AWARE;

bool Check(bool value, const char* message)
{
    if (!value)
        std::cerr << "FAILED: " << message << '\n';
    return value;
}

DISPLAYCONFIG_PATH_INFO MakePath(UINT32 sourceId, UINT32 targetId,
    UINT32 sourceModeIndex, UINT32 targetModeIndex, bool active, bool drrEnabled)
{
    DISPLAYCONFIG_PATH_INFO path{};
    path.sourceInfo.adapterId = LUID{ 11, 1 };
    path.sourceInfo.id = sourceId;
    path.sourceInfo.sourceModeInfoIdx = sourceModeIndex;
    path.targetInfo.adapterId = LUID{ 22, 2 };
    path.targetInfo.id = targetId;
    path.targetInfo.desktopModeInfoIdx = 0xFFFF;
    path.targetInfo.targetModeInfoIdx = targetModeIndex;
    path.targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL;
    path.targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
    path.targetInfo.scaling = DISPLAYCONFIG_SCALING_IDENTITY;
    path.targetInfo.refreshRate = DISPLAYCONFIG_RATIONAL{ drrEnabled ? 60u : 120u, 1 };
    path.targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    path.targetInfo.targetAvailable = TRUE;
    path.flags = (active ? DISPLAYCONFIG_PATH_ACTIVE : 0) |
        (drrEnabled ? DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE : 0);
    path.flags |= DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
    return path;
}

DISPLAYCONFIG_MODE_INFO MakeSourceMode(UINT32 id, UINT32 width = 1920,
    UINT32 height = 1080)
{
    DISPLAYCONFIG_MODE_INFO mode{};
    mode.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
    mode.id = id;
    mode.adapterId = LUID{ 11, 1 };
    mode.sourceMode.width = width;
    mode.sourceMode.height = height;
    mode.sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
    mode.sourceMode.position = POINTL{ 0, 0 };
    return mode;
}

DISPLAYCONFIG_MODE_INFO MakeTargetMode(UINT32 id, UINT32 height = 120)
{
    DISPLAYCONFIG_MODE_INFO mode{};
    mode.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
    mode.id = id;
    mode.adapterId = LUID{ 22, 2 };
    auto& signal = mode.targetMode.targetVideoSignalInfo;
    signal.pixelRate = 297000000;
    signal.hSyncFreq = DISPLAYCONFIG_RATIONAL{ 135000, 1 };
    signal.vSyncFreq = DISPLAYCONFIG_RATIONAL{ height, 1 };
    signal.activeSize = DISPLAYCONFIG_2DREGION{ 1920, 1080 };
    signal.totalSize = DISPLAYCONFIG_2DREGION{ 2200, 1125 };
    signal.videoStandard = 0;
    signal.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    return mode;
}

struct DisplayConfigFixture
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

DisplayConfigFixture OnePath(bool active, bool drrEnabled)
{
    return {
        { MakePath(1, 1, 0, 1, active, drrEnabled) },
        { MakeSourceMode(1), MakeTargetMode(1) }
    };
}

class FakeDisplayConfigApi final : public IWindowsDrrDisplayConfigApi
{
public:
    explicit FakeDisplayConfigApi(DisplayConfigFixture fixture)
        : currentPaths(std::move(fixture.paths)), currentModes(std::move(fixture.modes))
    {
    }

    LONG GetBufferSizes(UINT32 flags, UINT32* pathCount, UINT32* modeCount) override
    {
        getFlags.push_back(flags);
        ++bufferSizeCalls;
        if (bufferSizeStatus != ERROR_SUCCESS)
            return bufferSizeStatus;
        *pathCount = static_cast<UINT32>(currentPaths.size());
        *modeCount = static_cast<UINT32>(currentModes.size());
        return ERROR_SUCCESS;
    }

    LONG Query(UINT32 flags, UINT32* pathCount,
        DISPLAYCONFIG_PATH_INFO* paths, UINT32* modeCount,
        DISPLAYCONFIG_MODE_INFO* modes) override
    {
        queryFlags.push_back(flags);
        ++queryCalls;
        if (queryInsufficientBufferResponses > 0)
        {
            --queryInsufficientBufferResponses;
            *pathCount = static_cast<UINT32>(currentPaths.size());
            *modeCount = static_cast<UINT32>(currentModes.size());
            return ERROR_INSUFFICIENT_BUFFER;
        }
        if (applyCalls > 0 && verifyQueryStatus != ERROR_SUCCESS)
            return verifyQueryStatus;
        if (*pathCount < currentPaths.size() || *modeCount < currentModes.size())
        {
            *pathCount = static_cast<UINT32>(currentPaths.size());
            *modeCount = static_cast<UINT32>(currentModes.size());
            return ERROR_INSUFFICIENT_BUFFER;
        }

        *pathCount = static_cast<UINT32>(currentPaths.size());
        *modeCount = static_cast<UINT32>(currentModes.size());
        std::copy(currentPaths.begin(), currentPaths.end(), paths);
        std::copy(currentModes.begin(), currentModes.end(), modes);
        return ERROR_SUCCESS;
    }

    LONG Set(UINT32 pathCount, DISPLAYCONFIG_PATH_INFO* paths,
        UINT32 modeCount, DISPLAYCONFIG_MODE_INFO* modes, UINT32 flags) override
    {
        setFlags.push_back(flags);
        std::vector<DISPLAYCONFIG_PATH_INFO> requestedPaths(paths, paths + pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> requestedModes(modes, modes + modeCount);
        if ((flags & SDC_VALIDATE) != 0)
        {
            validatedPaths = std::move(requestedPaths);
            validatedModes = std::move(requestedModes);
            return validateStatus;
        }
        if ((flags & SDC_APPLY) == 0)
            return ERROR_INVALID_PARAMETER;

        ++applyCalls;
        appliedPaths = requestedPaths;
        appliedModes = requestedModes;
        if (applyStatus != ERROR_SUCCESS)
            return applyStatus;

        currentPaths = std::move(requestedPaths);
        currentModes = std::move(requestedModes);
        if (updateVirtualRefreshAfterApply)
            for (auto& path : currentPaths)
                if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0)
                    path.targetInfo.refreshRate = DISPLAYCONFIG_RATIONAL{ 120, 1 };
        if (keepBoostEnabledAfterApply)
            for (auto& path : currentPaths)
                if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0)
                    path.flags |= DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE;
        if (changeModeAfterApply && !currentModes.empty() &&
            currentModes.front().infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            ++currentModes.front().sourceMode.width;
        return ERROR_SUCCESS;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> currentPaths;
    std::vector<DISPLAYCONFIG_MODE_INFO> currentModes;
    std::vector<UINT32> getFlags;
    std::vector<UINT32> queryFlags;
    std::vector<UINT32> setFlags;
    std::vector<DISPLAYCONFIG_PATH_INFO> validatedPaths;
    std::vector<DISPLAYCONFIG_MODE_INFO> validatedModes;
    std::vector<DISPLAYCONFIG_PATH_INFO> appliedPaths;
    std::vector<DISPLAYCONFIG_MODE_INFO> appliedModes;
    int bufferSizeCalls{};
    int queryCalls{};
    int applyCalls{};
    int queryInsufficientBufferResponses{};
    LONG bufferSizeStatus{ ERROR_SUCCESS };
    LONG validateStatus{ ERROR_SUCCESS };
    LONG applyStatus{ ERROR_SUCCESS };
    LONG verifyQueryStatus{ ERROR_SUCCESS };
    bool keepBoostEnabledAfterApply{};
    bool changeModeAfterApply{};
    bool updateVirtualRefreshAfterApply{};
};
}

int main()
{
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    const std::wstring logDirectory = std::wstring(tempPath) + L"ClawHUD-WindowsDrrDisableTests";
    RuntimeLogger::SetDirectoryForTests(logDirectory);

    bool ok = true;
    {
        FakeDisplayConfigApi api(OnePath(true, true));
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::Applied,
            "one active DRR path validates, applies and verifies");
        ok &= Check(result.activePathCount == 1 && result.changedPathCount == 1,
            "single-path counts are reported");
        ok &= Check(api.setFlags.size() == 2 &&
            api.setFlags[0] == (kExpectedSetBaseFlags | SDC_VALIDATE) &&
            api.setFlags[1] == (kExpectedSetBaseFlags | SDC_APPLY | SDC_SAVE_TO_DATABASE),
            "one validate call precedes one persistent apply call");
        ok &= Check(api.validatedPaths.size() == 1 &&
            api.validatedPaths[0].flags ==
                (DISPLAYCONFIG_PATH_ACTIVE | DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE),
            "validation receives a path with only the boost bit removed");
        ok &= Check(api.appliedPaths.size() == 1 && api.appliedModes.size() == 2 &&
            api.appliedModes[0].sourceMode.width == 1920 &&
            api.appliedModes[1].targetMode.targetVideoSignalInfo.vSyncFreq.Numerator == 120,
            "apply preserves source and physical target modes");
        ok &= Check(api.getFlags == std::vector<UINT32>{ kExpectedReadFlags, kExpectedReadFlags } &&
            api.queryFlags == std::vector<UINT32>{ kExpectedReadFlags, kExpectedReadFlags },
            "both snapshots use the same active virtual-refresh-aware query flags");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, false));
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::AlreadyOff && api.setFlags.empty(),
            "DRR already off makes no SetDisplayConfig calls");
    }

    {
        DisplayConfigFixture fixture{
            {
                MakePath(1, 1, 0, 1, true, true),
                MakePath(2, 2, 2, 3, true, false),
                MakePath(3, 3, 4, 5, false, true),
            },
            {
                MakeSourceMode(1), MakeTargetMode(1),
                MakeSourceMode(2), MakeTargetMode(2),
                MakeSourceMode(3), MakeTargetMode(3),
            }
        };
        FakeDisplayConfigApi api(std::move(fixture));
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::Applied &&
            result.activePathCount == 2 && result.changedPathCount == 1,
            "multi-display operation counts only active paths and only changes DRR-on paths");
        ok &= Check(api.appliedPaths.size() == 3 &&
            api.appliedPaths[0].sourceInfo.id == 1 &&
            api.appliedPaths[1].sourceInfo.id == 2 &&
            api.appliedPaths[2].sourceInfo.id == 3 &&
            (api.appliedPaths[0].flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) == 0 &&
            (api.appliedPaths[1].flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) == 0 &&
            (api.appliedPaths[2].flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0,
            "path ordering is preserved, active DRR is cleared and inactive flags are retained");
        ok &= Check(api.appliedModes.size() == 6 &&
            api.appliedModes[0].sourceMode.width == 1920 &&
            api.appliedModes[5].targetMode.targetVideoSignalInfo.vSyncFreq.Numerator == 120,
            "all mode entries and their order are preserved");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.queryInsufficientBufferResponses = 1;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::Applied &&
            api.bufferSizeCalls == 3 && api.queryCalls == 3,
            "insufficient query buffers cause a bounded re-read before verification");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.bufferSizeStatus = ERROR_INVALID_PARAMETER;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::Unsupported &&
            api.queryCalls == 0 && api.setFlags.empty(),
            "unsupported virtual-refresh-aware query exits without applying a display change");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.validateStatus = ERROR_ACCESS_DENIED;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::ValidateFailed &&
            api.setFlags.size() == 1,
            "validation failure prevents the apply call");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.applyStatus = ERROR_BAD_CONFIGURATION;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::ApplyFailed &&
            api.setFlags.size() == 2 && api.queryCalls == 1,
            "apply failure is reported without claiming verification");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.keepBoostEnabledAfterApply = true;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::VerifyFailed &&
            api.setFlags.size() == 2 && api.queryCalls == 2,
            "a successful API return with DRR still enabled is not success");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.changeModeAfterApply = true;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::VerifyFailed,
            "changed source or target mode data fails post-apply verification");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.updateVirtualRefreshAfterApply = true;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::Applied &&
            api.currentPaths[0].targetInfo.refreshRate.Numerator == 120,
            "Windows may update virtual refresh as a result of DRR OFF without a forced mode change");
    }

    {
        FakeDisplayConfigApi api(OnePath(true, true));
        api.verifyQueryStatus = ERROR_ACCESS_DENIED;
        const auto result = DisableWindowsDrrForActivePaths(api);
        ok &= Check(result.status == DrrDisableStatus::VerifyFailed &&
            result.win32Error == ERROR_ACCESS_DENIED,
            "verification query errors are not reported as success");
    }

    RuntimeLogger::ResetForTests();
    std::error_code cleanupError;
    std::filesystem::remove_all(logDirectory, cleanupError);
    return ok ? 0 : 1;
}
