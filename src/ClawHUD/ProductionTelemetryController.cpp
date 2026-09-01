#include "ProductionTelemetryController.h"

#include "ProcessLiveness.h"
#include "RuntimeLogger.h"
#include "WindowsMemoryTelemetry.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace clawhud
{
namespace
{
constexpr UINT kUsageSamplingIntervalMs = 1000;
constexpr UINT kBatteryHudTimerIntervalMs = 5000;
constexpr UINT kPresentMonFpsSamplingIntervalMs = 500;
constexpr UINT kGraphicsApiRetryIntervalMs = 500;
constexpr unsigned kGraphicsApiMaxAttempts = 5;

void Log(const std::wstring& message)
{
    RuntimeLogger::Log(RuntimeLogLevel::Info, message);
}
}

ProductionTelemetryController::ProductionTelemetryController(
    PresentMonTelemetryProvider& provider)
    : provider_(provider)
{
}

void ProductionTelemetryController::Bind(HWND messageWindow,
    std::function<void()> requestRender)
{
    messageWindow_ = messageWindow;
    requestRender_ = std::move(requestRender);
}

void ProductionTelemetryController::FillSnapshot(HudTelemetrySnapshot& snapshot) const
{
    aggregator_.FillSnapshot(snapshot);
    snapshot.graphicsApi = latestGraphicsApi_;
    snapshot.presentMonDisplayedFps = latestProcessFps_;
    if (latestPower_)
    {
        snapshot.batteryPercent = latestPower_->batteryPercent;
        snapshot.onBattery = latestPower_->onBattery.value_or(false);
        if (snapshot.onBattery)
            snapshot.remainingMinutes = latestPower_->remainingMinutes;
    }
}

MsiEcHudTelemetry ProductionTelemetryController::ReadEcTelemetry()
{
    MsiEcHudTelemetry result{};
    if (!ecClient_)
        ecClient_ = std::make_unique<EcHelperClient>();

    const auto abortAfterFailure = [this]()
    {
        return ShouldAbortEcTelemetrySample(ecClient_->LastStage());
    };

    std::vector<std::uint8_t> payload;
    if (ecClient_->ReadTemperature(payload))
        result.cpuTempC = DecodeCpuTempC(payload);
    else if (abortAfterFailure())
    {
        ecClient_->Close();
        return result;
    }

    payload.clear();
    if (ecClient_->ReadFan(payload))
    {
        if (const auto fans = DecodeFanTelemetry(payload))
        {
            result.fan1Rpm = fans->fan1Rpm;
            result.fan2Rpm = fans->fan2Rpm;
        }
    }
    else if (abortAfterFailure())
    {
        ecClient_->Close();
        return result;
    }

    payload.clear();
    if (ecClient_->ReadData(221, payload))
        result.cpuPackagePowerW = DecodeCpuPackagePowerW(payload);
    else if (abortAfterFailure())
    {
        ecClient_->Close();
        return result;
    }

    if (!latestPower_ || !latestPower_->onBattery.value_or(false))
        return result;

    std::vector<std::uint8_t> currentLow;
    std::vector<std::uint8_t> currentHigh;
    std::vector<std::uint8_t> voltageLow;
    std::vector<std::uint8_t> voltageHigh;
    const bool c0 = ecClient_->ReadData(70, currentLow);
    if (!c0 || currentLow.empty())
    {
        if (abortAfterFailure())
            ecClient_->Close();
        return result;
    }
    const bool c1 = ecClient_->ReadData(71, currentHigh);
    if (!c1 || currentHigh.empty())
    {
        if (abortAfterFailure())
            ecClient_->Close();
        return result;
    }
    const bool v0 = ecClient_->ReadData(74, voltageLow);
    if (!v0 || voltageLow.empty())
    {
        if (abortAfterFailure())
            ecClient_->Close();
        return result;
    }
    const bool v1 = ecClient_->ReadData(75, voltageHigh);
    if (!v1 || voltageHigh.empty())
    {
        if (abortAfterFailure())
            ecClient_->Close();
        return result;
    }
    if (c0 && c1 && v0 && v1 && !currentLow.empty() && !currentHigh.empty() &&
        !voltageLow.empty() && !voltageHigh.empty())
    {
        const auto battery = DecodeBatteryPower(
            currentLow[0], currentHigh[0], voltageLow[0], voltageHigh[0]);
        if (battery)
            result.batteryDischargePowerW = battery->powerW;
    }
    return result;
}

void ProductionTelemetryController::SampleSystemEc()
{
    ReconcileGraphicsApiTargetLiveness();
    const auto freshEcTelemetry = ReadEcTelemetry();
    const bool onBattery = latestPower_ &&
        latestPower_->onBattery.value_or(false);
    const auto historyBefore = batteryEstimator_.SampleCount();
    if (onBattery && !batteryEcOnDc_)
    {
        batteryEcReadyLogged_ = false;
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[BatteryEC] DC sampling started");
    }
    if (!onBattery && batteryEcOnDc_)
    {
        batteryEcReadyLogged_ = false;
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[BatteryEC] history reset reason=ac-connected");
    }
    if (onBattery && historyBefore != 0 &&
        !freshEcTelemetry.batteryDischargePowerW)
    {
        batteryEstimator_.Reset();
        batteryEcReadyLogged_ = false;
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[BatteryEC] history reset reason=invalid-sample");
    }
    else
        batteryEstimator_.Observe(onBattery, freshEcTelemetry.batteryDischargePowerW,
            BatteryPowerEstimator::Clock::now());
    if (batteryEstimator_.Ready() && !batteryEcReadyLogged_)
    {
        batteryEcReadyLogged_ = true;
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[BatteryEC] estimator ready");
    }
    batteryEcOnDc_ = onBattery;
    aggregator_.IngestEc(freshEcTelemetry);
    const auto system = provider_.ReadSystem();
    const std::optional<double> missingDouble;
    const std::optional<std::uint64_t> missingBytes;
    HudSystemTelemetryInput systemInput;
    systemInput.cpuUsagePercent = system ? system->cpuUsagePercent : missingDouble;
    systemInput.gpuUsagePercent = system ? system->gpuUsagePercent : missingDouble;
    systemInput.gpuClockMHz = system ? system->gpuClockMHz : missingDouble;
    systemInput.gpuMemoryUsedBytes = system ? system->gpuMemoryUsedBytes : missingBytes;
    systemInput.systemMemoryUsedBytes = ReadSystemMemoryUsedBytes();
    aggregator_.IngestSystem(systemInput);
    if (requestRender_)
        requestRender_();
}

void ProductionTelemetryController::SampleBattery()
{
    latestPower_ = ReadWindowsPowerTelemetry();
    if (!latestPower_)
    {
        batteryEstimator_.Reset();
    }
    else
    {
        const auto ecEstimate = batteryEstimator_.EstimateRemainingMinutes(
            latestPower_->remainingCapacityMWh);
        latestPower_->remainingMinutes = ecEstimate;
        std::wostringstream batteryLog;
        batteryLog << L"[BatteryEC] source="
            << (ecEstimate ? L"EC" : L"none")
            << L" history=" << batteryEstimator_.SampleCount()
            << L" averageW=";
        if (const auto average = batteryEstimator_.AveragePowerW())
            batteryLog << std::fixed << std::setprecision(2) << *average;
        else
            batteryLog << L"unavailable";
        batteryLog << L" remainingCapacityMWh="
            << latestPower_->remainingCapacityMWh.value_or(0)
            << L" remainingMinutes="
            << latestPower_->remainingMinutes.value_or(0);
        RuntimeLogger::Log(RuntimeLogLevel::Debug, batteryLog.str());
    }
    if (requestRender_)
        requestRender_();
}

void ProductionTelemetryController::StartBaseSampling()
{
    if (samplingActive_)
        return;
    samplingActive_ = true;
    Log(L"Production telemetry sampling started");
    SampleSystemEc();
    SetTimer(messageWindow_, kEcHudTimerId, kUsageSamplingIntervalMs, nullptr);
    SampleBattery();
    SetTimer(messageWindow_, kBatteryHudTimerId, kBatteryHudTimerIntervalMs, nullptr);
}

void ProductionTelemetryController::SampleFps()
{
    const bool alwaysMode = visibilityMode_ == HudVisibilityMode::Always;
    const DWORD processId = ResolveProductionFpsTargetPid(
        visibilityMode_, alwaysFpsTarget_.TargetProcessId(),
        inGameForegroundProcessId_);
    const auto now = GetTickCount64();
    if (!processId)
    {
        if (latestProcessFps_)
            Log(L"[PresentMonFPS] target-cleared");
        latestProcessFps_.reset();
        fpsStaleHold_.Reset();
        provider_.ReadProcess(0);
        return;
    }
    const auto snapshot = provider_.ReadProcess(processId);
    const std::optional<double> freshFps =
        snapshot ? snapshot->displayedFps : std::nullopt;
    std::optional<double> targetFps;
    if (alwaysMode)
    {
        // Reject a result that no longer belongs to the current foreground PID
        // and never fall back to a background/In-Game Only game PID.
        alwaysFpsTarget_.AcceptSample(processId, freshFps);
        targetFps = alwaysFpsTarget_.DisplayedFps();
    }
    else
    {
        targetFps = freshFps;
    }
    // Retain the last valid FPS across brief same-PID misses; PID changes and
    // holds older than 2 s are discarded inside the stale hold.
    const bool wasHeld = latestProcessFps_ && !targetFps;
    latestProcessFps_ = fpsStaleHold_.Observe(processId, targetFps, now);
    if (wasHeld && !latestProcessFps_)
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[PresentMonFPS] pid=" + std::to_wstring(processId) +
            L" stale-expired");
    if (now - lastFpsCompareLogTick_ >= 1000)
    {
        lastFpsCompareLogTick_ = now;
        const auto fpsText = [](const std::optional<double>& value)
        {
            if (!value)
                return std::wstring(L"NA");
            std::wostringstream stream;
            stream << std::fixed << std::setprecision(2) << *value;
            return stream.str();
        };
        std::wstring line = L"[PresentMonFPS] pid=" + std::to_wstring(processId);
        if (snapshot && snapshot->swapChainAddress)
        {
            wchar_t address[19]{};
            swprintf_s(address, L"0x%016llX",
                static_cast<unsigned long long>(*snapshot->swapChainAddress));
            line += L" swap=" + std::wstring(address);
        }
        const std::optional<double> displayed =
            snapshot ? snapshot->displayedFps : std::nullopt;
        const std::optional<double> presented =
            snapshot ? snapshot->presentedFps : std::nullopt;
        line += L" displayed=" + fpsText(displayed) +
            L" presented=" + fpsText(presented);
        if (displayed && presented)
        {
            std::wostringstream delta;
            delta << std::fixed << std::setprecision(2)
                << (*displayed - *presented);
            line += L" delta=" + delta.str();
        }
        RuntimeLogger::Log(RuntimeLogLevel::Debug, line);
    }
    if (requestRender_)
        requestRender_();
}

void ProductionTelemetryController::StartFpsSampling()
{
    if (!provider_.Ready() ||
        !provider_.ProcessReady())
    {
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[PresentMonFPS] provider-unavailable");
        return;
    }
    SampleFps();
    SetTimer(messageWindow_, kPresentMonFpsTimerId,
        kPresentMonFpsSamplingIntervalMs, nullptr);
}

void ProductionTelemetryController::StopFpsSampling(bool clearTarget)
{
    KillTimer(messageWindow_, kPresentMonFpsTimerId);
    if (clearTarget)
    {
        (void)provider_.ReadProcess(0);
        latestProcessFps_.reset();
        fpsStaleHold_.Reset();
    }
}

void ProductionTelemetryController::StopSamplingTimersAndFps()
{
    KillTimer(messageWindow_, kEcHudTimerId);
    KillTimer(messageWindow_, kBatteryHudTimerId);
    StopFpsSampling();
}

void ProductionTelemetryController::ResetSamplingState(const wchar_t* reason)
{
    const bool wasActive = samplingActive_;
    if (ecClient_)
    {
        ecClient_->Close();
        ecClient_.reset();
    }
    aggregator_.Reset();
    latestPower_.reset();
    batteryEstimator_.Reset();
    batteryEcOnDc_ = false;
    batteryEcReadyLogged_ = false;
    samplingActive_ = false;
    if (wasActive)
        Log(L"Production telemetry sampling stopped reason=" + std::wstring(reason));
}

void ProductionTelemetryController::SetVisibilityMode(HudVisibilityMode mode,
    DWORD currentForegroundProcessId)
{
    // Mode switching only changes FPS target authority; it never creates or
    // destroys the shared PresentMon API2 provider path.
    visibilityMode_ = mode;
    alwaysFpsTarget_.Release();
    latestProcessFps_.reset();
    fpsStaleHold_.Reset();
    if (mode == HudVisibilityMode::Always)
    {
        // Adopt the currently known foreground PID immediately instead of
        // waiting for the next foreground-change event.
        alwaysFpsTarget_.SetForegroundProcess(currentForegroundProcessId);
    }
}

void ProductionTelemetryController::OnForegroundProcessChanged(DWORD processId)
{
    if (alwaysFpsTarget_.SetForegroundProcess(processId) &&
        visibilityMode_ == HudVisibilityMode::Always)
    {
        // Foreground PID changed: the previous PID's FPS is invalid
        // immediately, never leaking across the target change, and the
        // same-PID stale hold must not carry it either. No extra render
        // here; the normal visibility/sampling path performs the HUD
        // update.
        latestProcessFps_.reset();
        fpsStaleHold_.Reset();
        Log(L"[PresentMonFPS] mode=Always foregroundPid=" +
            std::to_wstring(processId) + L" fps-invalidated");
    }
}

void ProductionTelemetryController::SetInGameForegroundProcess(DWORD processId)
{
    // No numeric-PID early return: the caller (GameSessionController) only
    // calls this for a genuine ForegroundGameTargetAction::SetEligible - a new
    // exact GameProcessInstance. Windows PID reuse can make that a *different*
    // process generation while the numeric PID stays the same, so gating on
    // PID equality here would silently keep a prior generation's stale FPS.
    inGameForegroundProcessId_ = processId;
    // An FPS value retained for the previous target must never be displayed
    // for the new one. The same-PID stale hold is only meant to bridge brief
    // misses for one unchanged target. But this target keeps changing under
    // Always mode too (game detection is visibility-mode independent), so
    // only invalidate the shared FPS state/query when InGameOnly is actually
    // its active authority - Always mode's FPS must stay fully decoupled from
    // game-detection transitions.
    if (InGameTargetChangeInvalidatesFps(visibilityMode_))
    {
        latestProcessFps_.reset();
        fpsStaleHold_.Reset();
        // PresentMonProcessTelemetry only rebuilds its target-bound query when
        // the numeric PID actually changes; PID reuse (same PID, new process
        // generation) would otherwise keep the old generation's frame-metric
        // tracking/query alive. Releasing the target here forces the next
        // ReadProcess(processId) in SampleFps() to retarget cleanly.
        (void)provider_.ReadProcess(0);
    }
    Log(L"[PresentMonFPS] mode=InGameOnly targetPid=" +
        std::to_wstring(processId));
}

void ProductionTelemetryController::ClearInGameForegroundProcess()
{
    if (inGameForegroundProcessId_ == 0)
        return;
    inGameForegroundProcessId_ = 0;
    if (InGameTargetChangeInvalidatesFps(visibilityMode_))
    {
        latestProcessFps_.reset();
        fpsStaleHold_.Reset();
        (void)provider_.ReadProcess(0);
    }
    Log(L"[PresentMonFPS] mode=InGameOnly target-cleared");
}

void ProductionTelemetryController::StartGraphicsApiProbe(DWORD processId)
{
    StopGraphicsApiProbe();
    graphicsApiProcessId_ = processId;
    TryGraphicsApiProbe();
}

void ProductionTelemetryController::EnsureGraphicsApiProbe(DWORD processId)
{
    if (graphicsApiProcessId_ != processId)
        StartGraphicsApiProbe(processId);
}

void ProductionTelemetryController::StopGraphicsApiProbe()
{
    KillTimer(messageWindow_, kGraphicsApiRetryTimerId);
    graphicsApiProbe_.Reset();
    graphicsApiProcessId_ = 0;
    graphicsApiAttempts_ = 0;
    latestGraphicsApi_.reset();
}

void ProductionTelemetryController::StopGraphicsApiProbeIfTarget(DWORD processId)
{
    if (graphicsApiProcessId_ == processId)
        StopGraphicsApiProbe();
}

void ProductionTelemetryController::ReconcileGraphicsApiTargetLiveness()
{
    if (graphicsApiProcessId_ && !ProcessAlive(graphicsApiProcessId_))
        StopGraphicsApiProbe();
}

void ProductionTelemetryController::TryGraphicsApiProbe()
{
    if (!graphicsApiProcessId_ || !ProcessAlive(graphicsApiProcessId_))
    {
        StopGraphicsApiProbe();
        return;
    }
    ++graphicsApiAttempts_;
    latestGraphicsApi_ = graphicsApiProbe_.Query(graphicsApiProcessId_);
    if (latestGraphicsApi_ || graphicsApiAttempts_ >= kGraphicsApiMaxAttempts)
    {
        KillTimer(messageWindow_, kGraphicsApiRetryTimerId);
        graphicsApiProbe_.Reset();
        if (!latestGraphicsApi_)
            RuntimeLogger::Log(RuntimeLogLevel::Warn,
                L"IGCL Graphics API unresolved after bounded retries");
        else
            Log(L"Graphics API resolved api=" + *latestGraphicsApi_);
        if (requestRender_)
            requestRender_();
        return;
    }
    SetTimer(messageWindow_, kGraphicsApiRetryTimerId,
        kGraphicsApiRetryIntervalMs, nullptr);
}
}
