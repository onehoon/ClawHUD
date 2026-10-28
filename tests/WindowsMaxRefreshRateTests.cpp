#include "Tweaks/WindowsDrr/WindowsMaxRefreshRate.h"
#include "Tweaks/VrrDiagnosticLogger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace clawhud;
namespace
{
constexpr DWORD kModeFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY |
    DM_BITSPERPEL | DM_DISPLAYORIENTATION | DM_DISPLAYFLAGS | DM_POSITION;

bool Check(bool value, const char* message)
{
    if (!value)
        std::cerr << "FAILED: " << message << '\n';
    return value;
}

DEVMODEW Mode(unsigned hz, DWORD width = 1920, DWORD height = 1080,
    DWORD bpp = 32, DWORD orientation = DMDO_DEFAULT, DWORD flags = 0,
    LONG x = 0, LONG y = 0)
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    mode.dmFields = kModeFields;
    mode.dmPelsWidth = width;
    mode.dmPelsHeight = height;
    mode.dmBitsPerPel = bpp;
    mode.dmDisplayFrequency = hz;
    mode.dmDisplayOrientation = orientation;
    mode.dmDisplayFlags = flags;
    mode.dmPosition = POINTL{ x, y };
    return mode;
}

WindowsActiveDisplayPath Path(UINT32 sourceId = 1, UINT32 targetId = 1,
    const wchar_t* gdi = LR"(\\.\DISPLAY1)", unsigned hz = 60)
{
    WindowsActiveDisplayPath path{};
    path.sourceAdapter = LUID{ 7, 1 };
    path.sourceId = sourceId;
    path.targetAdapter = LUID{ 9, 2 };
    path.targetId = targetId;
    path.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL;
    path.flags = DISPLAYCONFIG_PATH_ACTIVE;
    path.virtualRefresh = DISPLAYCONFIG_RATIONAL{ hz, 1 };
    path.physicalRefresh = DISPLAYCONFIG_RATIONAL{ hz, 1 };
    path.gdiDeviceName = gdi;
    path.monitorDevicePath = L"\\\\?\\DISPLAY#TEST#1";
    path.monitorName = L"Test Monitor";
    path.sourceNameValid = true;
    path.attachedToDesktop = true;
    path.targetAvailable = true;
    path.advancedColorKnown = true;
    return path;
}

struct Device
{
    DEVMODEW current{ Mode(60) };
    std::vector<DEVMODEW> modes{ Mode(60), Mode(120), Mode(180) };
};

struct ChangeCall
{
    std::wstring device;
    DWORD flags{};
    unsigned hz{};
};

class FakeRefreshApi final : public IWindowsMaxRefreshRateApi
{
public:
    LONG ReadActiveDisplayPaths(std::vector<WindowsActiveDisplayPath>& output) override
    {
        ++pathReads;
        if (readStatus != ERROR_SUCCESS)
            return readStatus;
        if (driftTopologyAtRead > 0 && pathReads == driftTopologyAtRead && !paths.empty())
            ++paths.front().targetId;
        if (driftTargetAvailabilityAtRead > 0 &&
            pathReads == driftTargetAvailabilityAtRead && !paths.empty())
            paths.front().targetAvailable = !paths.front().targetAvailable;
        output = paths;
        return ERROR_SUCCESS;
    }

    bool EnumDisplaySettings(const std::wstring& deviceName,
        DWORD index, DEVMODEW& mode) override
    {
        auto found = devices.find(deviceName);
        if (found == devices.end())
            return false;
        if (index == ENUM_CURRENT_SETTINGS)
        {
            if (currentModeUnavailable)
                return false;
            mode = found->second.current;
            return true;
        }
        if (endlessModes && index < 4096)
        {
            mode = Mode(180);
            return true;
        }
        if (index >= found->second.modes.size())
            return false;
        mode = found->second.modes[index];
        return true;
    }

    LONG ChangeDisplaySettings(const wchar_t* deviceName,
        DEVMODEW* mode, DWORD flags) override
    {
        const std::wstring name = deviceName ? deviceName : L"<all>";
        calls.push_back({ name, flags, mode ? mode->dmDisplayFrequency : 0 });
        if ((flags & CDS_TEST) != 0)
        {
            const auto test = testResults.find(mode->dmDisplayFrequency);
            return test == testResults.end() ? defaultTestStatus : test->second;
        }
        if (!deviceName)
        {
            ++commitCalls;
            const LONG status = commitStatuses.empty()
                ? DISP_CHANGE_SUCCESSFUL : Pop(commitStatuses);
            if (status != DISP_CHANGE_SUCCESSFUL)
                return status;
            for (const auto& [stagedName, stagedMode] : staged)
                Apply(stagedName, stagedMode);
            staged.clear();
            return status;
        }

        if (stageFailureDevice == name && (flags & CDS_NORESET) != 0)
            return stageFailureStatus;
        if (flags & CDS_NORESET)
        {
            staged[name] = *mode;
            return DISP_CHANGE_SUCCESSFUL;
        }
        const LONG applyStatus = directApplyStatuses.empty()
            ? directApplyStatus : Pop(directApplyStatuses);
        if (applyStatus != DISP_CHANGE_SUCCESSFUL)
            return applyStatus;
        Apply(name, *mode);
        return DISP_CHANGE_SUCCESSFUL;
    }

    void AddDevice(const std::wstring& name, Device device = {})
    {
        devices[name] = std::move(device);
        for (auto& path : paths)
            if (path.gdiDeviceName == name)
                path.physicalRefresh = DISPLAYCONFIG_RATIONAL{
                    devices[name].current.dmDisplayFrequency, 1 };
    }

    void Apply(const std::wstring& name, const DEVMODEW& mode)
    {
        auto found = devices.find(name);
        if (found == devices.end())
            return;
        if (mode.dmFields == DM_DISPLAYFREQUENCY)
            found->second.current.dmDisplayFrequency = mode.dmDisplayFrequency;
        else
            found->second.current = mode;
        if (driftResolutionOnApply && mode.dmFields == DM_DISPLAYFREQUENCY)
            ++found->second.current.dmPelsWidth;
        for (auto& path : paths)
            if (path.gdiDeviceName == name)
            {
                path.physicalRefresh = DISPLAYCONFIG_RATIONAL{
                    found->second.current.dmDisplayFrequency, 1 };
                if (fractionalPhysicalRefreshOnApply &&
                    found->second.current.dmDisplayFrequency == 120)
                    path.physicalRefresh = DISPLAYCONFIG_RATIONAL{ 119880, 1000 };
                if (reenableBoostOnApply)
                    path.flags |= DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE;
                if (driftAdvancedColorOnApply)
                    path.advancedColorEnabled = !path.advancedColorEnabled;
            }
    }

    static LONG Pop(std::vector<LONG>& values)
    {
        const LONG value = values.front();
        values.erase(values.begin());
        return value;
    }

    std::vector<WindowsActiveDisplayPath> paths{ Path() };
    std::map<std::wstring, Device> devices{ { LR"(\\.\DISPLAY1)", Device{} } };
    std::map<unsigned, LONG> testResults;
    std::vector<LONG> commitStatuses;
    std::vector<LONG> directApplyStatuses;
    std::map<std::wstring, DEVMODEW> staged;
    std::vector<ChangeCall> calls;
    std::wstring stageFailureDevice;
    LONG readStatus{ ERROR_SUCCESS };
    LONG defaultTestStatus{ DISP_CHANGE_SUCCESSFUL };
    LONG directApplyStatus{ DISP_CHANGE_SUCCESSFUL };
    LONG stageFailureStatus{ DISP_CHANGE_FAILED };
    int pathReads{};
    int commitCalls{};
    int driftTopologyAtRead{};
    int driftTargetAvailabilityAtRead{};
    bool currentModeUnavailable{};
    bool endlessModes{};
    bool driftResolutionOnApply{};
    bool fractionalPhysicalRefreshOnApply{};
    bool reenableBoostOnApply{};
    bool driftAdvancedColorOnApply{};
};

unsigned CountCalls(const FakeRefreshApi& api, DWORD flag)
{
    return static_cast<unsigned>(std::count_if(api.calls.begin(), api.calls.end(),
        [flag](const auto& call) { return (call.flags & flag) != 0; }));
}
}

int main()
{
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    const std::wstring logDirectory = std::wstring(tempPath) + L"ClawHUD-WindowsMaxRefreshRateTests";
    VrrDiagnosticLogger::SetDirectoryForTests(logDirectory);
    const auto runId = VrrDiagnosticLogger::BeginRun(true, "test", "1");

    bool ok = true;
    {
        DISPLAYCONFIG_PATH_INFO nativePath{};
        nativePath.flags = DISPLAYCONFIG_PATH_ACTIVE;
        nativePath.targetInfo.modeInfoIdx = 3;
        DISPLAYCONFIG_MODE_INFO nativeModes[4]{};
        nativeModes[0].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        nativeModes[0].targetMode.targetVideoSignalInfo.vSyncFreq =
            DISPLAYCONFIG_RATIONAL{ 120000, 1000 };
        nativeModes[3].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        nativeModes[3].targetMode.targetVideoSignalInfo.vSyncFreq =
            DISPLAYCONFIG_RATIONAL{ 165000, 1000 };
        DISPLAYCONFIG_RATIONAL selected{};
        const bool selectedNonVirtual = detail::TryGetTargetPhysicalRefresh(
            nativePath, nativeModes, 4, selected);
        ok &= Check(selectedNonVirtual && selected.Numerator == 165000 &&
            selected.Denominator == 1000,
            "non-virtual path reads the full-width modeInfoIdx instead of union high bits");

        nativePath.flags |= DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
        nativePath.targetInfo.targetModeInfoIdx = 2;
        nativeModes[2].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        nativeModes[2].targetMode.targetVideoSignalInfo.vSyncFreq =
            DISPLAYCONFIG_RATIONAL{ 144000, 1000 };
        const bool selectedVirtual = detail::TryGetTargetPhysicalRefresh(
            nativePath, nativeModes, 4, selected);
        ok &= Check(selectedVirtual && selected.Numerator == 144000 &&
            selected.Denominator == 1000,
            "virtual path reads targetModeInfoIdx from the virtual-mode union");
    }

    {
        FakeRefreshApi api;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api, true);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Applied &&
            result.appliedCount == 1 && result.targets[0].maximumHz == 180 &&
            result.finalDrrInvariantChecked && !result.finalDrrInvariantFailed,
            "DRR already off still applies the highest compatible fixed rate");
        ok &= Check(api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 180 &&
            CountCalls(api, CDS_UPDATEREGISTRY) == 1 && api.commitCalls == 0,
            "single display applies once without batch commit");
        const auto secondRun = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(secondRun.status == WindowsMaxRefreshStatus::AlreadyMax &&
            CountCalls(api, CDS_UPDATEREGISTRY) == 1,
            "a second startup is idempotent and does not repeat a mode write");
    }

    {
        FakeRefreshApi api;
        auto device = Device{};
        device.current = Mode(180);
        device.modes = { Mode(60), Mode(120), Mode(180) };
        api.AddDevice(LR"(\\.\DISPLAY1)", device);
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::AlreadyMax &&
            CountCalls(api, CDS_TEST) == 0 && CountCalls(api, CDS_UPDATEREGISTRY) == 0,
            "already maximum mode makes no display change call");
    }

    {
        FakeRefreshApi api;
        auto device = Device{};
        device.current = Mode(200);
        device.modes = { Mode(60), Mode(120), Mode(180) };
        api.AddDevice(LR"(\\.\DISPLAY1)", device);
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::AlreadyMax &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 200 &&
            api.calls.empty(), "a current rate above enumerated modes is never downgraded");
    }

    {
        FakeRefreshApi api;
        auto device = Device{};
        device.modes = { Mode(60), Mode(120), Mode(240, 2560, 1440),
            Mode(165, 1920, 1080, 24), Mode(144, 1920, 1080, 32, DMDO_90),
            Mode(1), Mode(120, 1920, 1080, 32, DMDO_DEFAULT, DM_INTERLACED) };
        api.AddDevice(LR"(\\.\DISPLAY1)", device);
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Applied &&
            result.targets[0].maximumHz == 120,
            "resolution orientation depth scan flags and default frequency filter candidates");
    }

    {
        FakeRefreshApi api;
        auto device = Device{};
        device.modes = { Mode(60, 1600, 900), Mode(120, 1600, 900) };
        api.AddDevice(LR"(\\.\DISPLAY1)", device);
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::NoCompatibleMode &&
            CountCalls(api, CDS_UPDATEREGISTRY) == 0,
            "a higher mode at a different current resolution is not selected");
    }

    {
        FakeRefreshApi api;
        api.testResults[180] = DISP_CHANGE_BADMODE;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Applied &&
            result.targets[0].maximumHz == 120 &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 120,
            "rejected maximum falls back to the next highest testable compatible mode");
    }

    {
        FakeRefreshApi api;
        api.testResults[180] = DISP_CHANGE_BADMODE;
        api.testResults[120] = DISP_CHANGE_BADMODE;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::ValidateFailed &&
            result.failedCount == 1 && CountCalls(api, CDS_UPDATEREGISTRY) == 0,
            "all rejected compatible modes fail cleanly without applying");
    }

    {
        FakeRefreshApi api;
        api.endlessModes = true;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::NoCompatibleMode &&
            result.targets[0].candidateCount == 4096 &&
            CountCalls(api, CDS_UPDATEREGISTRY) == 0,
            "mode enumeration reaching its bound is treated as incomplete");
    }

    {
        FakeRefreshApi api;
        api.paths[0].flags |= DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Applied &&
            (api.paths[0].flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0,
            "refresh stage can apply independently while DRR remains enabled");
    }

    {
        FakeRefreshApi api;
        api.endlessModes = true;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::NoCompatibleMode &&
            result.targets[0].candidateCount == 4096 &&
            CountCalls(api, CDS_UPDATEREGISTRY) == 0,
            "mode enumeration reaching its bound is treated as incomplete");
    }

    {
        FakeRefreshApi api;
        api.paths[0].flags |= DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Applied &&
            (api.paths[0].flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0,
            "refresh stage can apply independently while DRR remains enabled");
    }

    {
        FakeRefreshApi api;
        api.paths.push_back(Path(2, 2, LR"(\\.\DISPLAY2)", 60));
        api.paths.back().sourceAdapter = LUID{ 8, 1 };
        api.paths.back().targetAdapter = LUID{ 10, 2 };
        Device second{};
        second.current = Mode(60, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0);
        second.modes = { Mode(60, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0),
            Mode(165, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0) };
        api.AddDevice(LR"(\\.\DISPLAY2)", second);
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Applied &&
            result.appliedCount == 2 && api.commitCalls == 1 &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 180 &&
            api.devices[LR"(\\.\DISPLAY2)"].current.dmDisplayFrequency == 165,
            "independent displays stage per-device modes then commit once");
    }

    {
        FakeRefreshApi api;
        api.paths.push_back(Path(1, 2, LR"(\\.\DISPLAY1)", 60));
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::SkippedUnsafeTopology &&
            result.skippedCount == 1 && api.calls.empty(),
            "clone source is skipped rather than choosing one target's maximum");
    }

    {
        FakeRefreshApi api;
        api.paths[0].targetAvailable = false;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::SkippedUnsafeTopology &&
            result.targets[0].status == WindowsMaxRefreshStatus::SkippedUnsafeTopology &&
            result.skippedCount == 1 && api.calls.empty(),
            "active path with an unavailable target is skipped without test or apply calls");
    }

    {
        FakeRefreshApi api;
        api.driftTargetAvailabilityAtRead = 2;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::SkippedUnsafeTopology &&
            CountCalls(api, CDS_UPDATEREGISTRY) == 0,
            "target availability drift before apply fails topology verification closed");
    }

    {
        FakeRefreshApi api;
        api.paths[0].remoteSession = true;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::SkippedUnsafeTopology &&
            api.calls.empty(), "remote session does not receive a display mode write");
    }

    {
        FakeRefreshApi api;
        api.driftTopologyAtRead = 2;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::SkippedUnsafeTopology &&
            CountCalls(api, CDS_UPDATEREGISTRY) == 0,
            "changed target identity is rechecked before applying a mode");
    }

    {
        FakeRefreshApi api;
        api.driftResolutionOnApply = true;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::VerifyFailed &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmPelsWidth == 1920 &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 60,
            "layout drift fails verification and restores the original mode");
    }

    {
        FakeRefreshApi api;
        api.testResults[180] = DISP_CHANGE_BADMODE;
        api.fractionalPhysicalRefreshOnApply = true;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Applied &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 120,
            "integer DEVMODE rate accepts a nearby rational physical refresh");
    }

    {
        FakeRefreshApi api;
        api.driftAdvancedColorOnApply = true;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::VerifyFailed &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 60,
            "advanced-color drift fails verification and triggers a bounded mode restore");
    }

    {
        FakeRefreshApi api;
        api.reenableBoostOnApply = true;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api, true);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Partial &&
            (api.paths[0].flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0 &&
            result.finalDrrInvariantFailed,
            "DRR boost state change is reported as a separate verification failure");
    }

    {
        FakeRefreshApi api;
        api.stageFailureDevice = LR"(\\.\DISPLAY2)";
        api.paths.push_back(Path(2, 2, LR"(\\.\DISPLAY2)", 60));
        api.paths.back().sourceAdapter = LUID{ 8, 1 };
        api.paths.back().targetAdapter = LUID{ 10, 2 };
        Device second{};
        second.current = Mode(60, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0);
        second.modes = { Mode(60, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0),
            Mode(165, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0) };
        api.AddDevice(LR"(\\.\DISPLAY2)", second);
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::ApplyFailed &&
            api.commitCalls == 1 && api.staged.empty() &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 60 &&
            api.devices[LR"(\\.\DISPLAY2)"].current.dmDisplayFrequency == 60,
            "partial batch staging restores original settings before returning");
    }

    {
        FakeRefreshApi api;
        api.paths.push_back(Path(2, 2, LR"(\\.\DISPLAY2)", 60));
        api.paths.back().sourceAdapter = LUID{ 8, 1 };
        api.paths.back().targetAdapter = LUID{ 10, 2 };
        Device second{};
        second.current = Mode(60, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0);
        second.modes = { Mode(60, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0),
            Mode(165, 2560, 1440, 32, DMDO_DEFAULT, 0, 1920, 0) };
        api.AddDevice(LR"(\\.\DISPLAY2)", second);
        api.commitStatuses = { DISP_CHANGE_FAILED, DISP_CHANGE_SUCCESSFUL };
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::ApplyFailed &&
            api.commitCalls == 2 && api.staged.empty() &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 60 &&
            api.devices[LR"(\\.\DISPLAY2)"].current.dmDisplayFrequency == 60,
            "failed multi-display commit restores the saved original modes");
    }

    {
        FakeRefreshApi api;
        api.readStatus = ERROR_ACCESS_DENIED;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::Unavailable &&
            api.calls.empty(), "active path query failure causes no mode change");
    }

    {
        FakeRefreshApi api;
        api.directApplyStatus = DISP_CHANGE_RESTART;
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::ApplyFailed &&
            result.targets[0].displayChangeCode == DISP_CHANGE_RESTART &&
            api.devices[LR"(\\.\DISPLAY1)"].current.dmDisplayFrequency == 60,
            "restart-required apply is distinct from a successful dynamic change");
    }

    {
        FakeRefreshApi api;
        api.paths[0].sourceNameValid = false;
        api.paths[0].gdiDeviceName.clear();
        const auto result = MaximizeFixedRefreshRateForActiveDisplays(api);
        ok &= Check(result.status == WindowsMaxRefreshStatus::SkippedUnsafeTopology &&
            api.calls.empty(), "an unresolved GDI source never falls through to the primary display");
    }

    std::ifstream logFile(std::filesystem::path(logDirectory) / L"tweaks-vrr.log",
        std::ios::binary);
    const std::string log((std::istreambuf_iterator<char>(logFile)),
        std::istreambuf_iterator<char>());
    ok &= Check(log.find("[" + runId + "] [RefreshRate]") != std::string::npos &&
        log.find("modeBefore=1920x1080 60Hz") != std::string::npos &&
        log.find("compatibleCandidates=") != std::string::npos &&
        log.find("rollbackVerified=") != std::string::npos &&
        log.find("reason=TargetUnavailable") != std::string::npos,
        "stage diagnostics share the VRR run ID and include candidates and rollback");
    ok &= Check(!std::filesystem::exists(std::filesystem::path(logDirectory) / L"clawhud.log"),
        "refresh-rate details are not copied to the general runtime log");

    VrrDiagnosticLogger::ResetForTests();
    std::error_code cleanupError;
    std::filesystem::remove_all(logDirectory, cleanupError);
    return ok ? 0 : 1;
}
