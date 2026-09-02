# ClawHUD test targets. Relocated verbatim from the root CMakeLists.txt
# if(BUILD_TESTING) block (R8). The root file owns the BUILD_TESTING
# condition and includes this file; declarations here are unchanged.

    add_executable(ClawHUD.HudModelTests
        tests/HudModelTests.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.HudModelTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.HudModelTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.HudModelTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudModelTests COMMAND ClawHUD.HudModelTests)

    add_executable(ClawHUD.ControlProtocolTests
        tests/ClawHudControlCodecTests.cpp
        src/shared/ClawHudControlCodec.cpp)
    target_compile_features(ClawHUD.ControlProtocolTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.ControlProtocolTests PRIVATE src/shared)
    set_target_properties(ClawHUD.ControlProtocolTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ControlProtocolTests COMMAND ClawHUD.ControlProtocolTests)

    add_executable(ClawHUD.RuntimeControlDispatchTests
        tests/RuntimeControlDispatchTests.cpp
        src/ClawHUD/RuntimeControlDispatchBridge.cpp
        src/ClawHUD/RuntimeControlWireMapping.cpp
        src/ClawHUD/HudModel.cpp
        src/shared/ClawHudControlCodec.cpp)
    target_compile_features(ClawHUD.RuntimeControlDispatchTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.RuntimeControlDispatchTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.RuntimeControlDispatchTests PRIVATE src/ClawHUD src/shared)
    set_target_properties(ClawHUD.RuntimeControlDispatchTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.RuntimeControlDispatchTests COMMAND ClawHUD.RuntimeControlDispatchTests)

    add_executable(ClawHUD.RuntimeLoggerTests
        tests/RuntimeLoggerTests.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.RuntimeLoggerTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.RuntimeLoggerTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX CLAWHUD_RUNTIME_LOGGER_TESTS)
    target_include_directories(ClawHUD.RuntimeLoggerTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.RuntimeLoggerTests PRIVATE shell32 ole32)
    set_target_properties(ClawHUD.RuntimeLoggerTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.RuntimeLoggerTests COMMAND ClawHUD.RuntimeLoggerTests)

    add_executable(ClawHUD.EcHelperClientRuntimeLoggingTests
        tests/EcHelperClientRuntimeLoggingTests.cpp
        src/ClawHUD/EcHelperClient.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.EcHelperClientRuntimeLoggingTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.EcHelperClientRuntimeLoggingTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX CLAWHUD_RUNTIME_LOGGER_TESTS)
    target_include_directories(ClawHUD.EcHelperClientRuntimeLoggingTests PRIVATE
        src/ClawHUD src/shared)
    target_link_libraries(ClawHUD.EcHelperClientRuntimeLoggingTests PRIVATE shell32 ole32)
    set_target_properties(ClawHUD.EcHelperClientRuntimeLoggingTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.EcHelperClientRuntimeLoggingTests
        COMMAND ClawHUD.EcHelperClientRuntimeLoggingTests)

    add_executable(ClawHUD.HudRendererTests
        tests/HudRendererTests.cpp
        src/ClawHUD/HudRenderer.cpp
        src/ClawHUD/RuntimeLogger.cpp
        src/ClawHUD/HudWindowGeometry.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.HudRendererTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.HudRendererTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        CLAWHUD_TEST_UNISPACE_PATH=L"${CMAKE_SOURCE_DIR}/third_party/unispace/Unispace.otf")
    target_include_directories(ClawHUD.HudRendererTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.HudRendererTests PRIVATE d2d1 dwrite d3d11 dxgi shell32 ole32)
    set_target_properties(ClawHUD.HudRendererTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudRendererTests COMMAND ClawHUD.HudRendererTests)

    add_executable(ClawHUD.FpsStaleHoldTests
        tests/FpsStaleHoldTests.cpp
        src/ClawHUD/FpsStaleHold.cpp)
    target_compile_features(ClawHUD.FpsStaleHoldTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.FpsStaleHoldTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.FpsStaleHoldTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.FpsStaleHoldTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.FpsStaleHoldTests COMMAND ClawHUD.FpsStaleHoldTests)

    add_executable(ClawHUD.Win32FormatTests
        tests/Win32FormatTests.cpp
        src/ClawHUD/Win32Format.cpp)
    target_compile_features(ClawHUD.Win32FormatTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.Win32FormatTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.Win32FormatTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.Win32FormatTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.Win32FormatTests COMMAND ClawHUD.Win32FormatTests)

    add_executable(ClawHUD.ProcessLivenessTests
        tests/ProcessLivenessTests.cpp
        src/ClawHUD/ProcessLiveness.cpp)
    target_compile_features(ClawHUD.ProcessLivenessTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.ProcessLivenessTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.ProcessLivenessTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.ProcessLivenessTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ProcessLivenessTests COMMAND ClawHUD.ProcessLivenessTests)

    add_executable(ClawHUD.HudTelemetryAggregatorTests
        tests/HudTelemetryAggregatorTests.cpp
        src/ClawHUD/HudTelemetryAggregator.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.HudTelemetryAggregatorTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.HudTelemetryAggregatorTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.HudTelemetryAggregatorTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.HudTelemetryAggregatorTests PRIVATE user32)
    set_target_properties(ClawHUD.HudTelemetryAggregatorTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudTelemetryAggregatorTests COMMAND ClawHUD.HudTelemetryAggregatorTests)

    add_executable(ClawHUD.HudSettingsStoreTests
        tests/HudSettingsStoreTests.cpp
        src/ClawHUD/HudSettingsStore.cpp
        src/ClawHUD/HudSize.cpp
        src/ClawHUD/HudModel.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.HudSettingsStoreTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.HudSettingsStoreTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX CLAWHUD_RUNTIME_LOGGER_TESTS)
    target_include_directories(ClawHUD.HudSettingsStoreTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.HudSettingsStoreTests PRIVATE shell32 ole32 user32)
    set_target_properties(ClawHUD.HudSettingsStoreTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudSettingsStoreTests COMMAND ClawHUD.HudSettingsStoreTests)

    add_executable(ClawHUD.AlwaysModeFpsTargetTests
        tests/AlwaysModeFpsTargetTests.cpp
        src/ClawHUD/AlwaysModeFpsTarget.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.AlwaysModeFpsTargetTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.AlwaysModeFpsTargetTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.AlwaysModeFpsTargetTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.AlwaysModeFpsTargetTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.AlwaysModeFpsTargetTests COMMAND ClawHUD.AlwaysModeFpsTargetTests)

    add_executable(ClawHUD.ProductionTargetPolicyTests
        tests/ProductionTargetPolicyTests.cpp
        src/ClawHUD/ProductionTargetPolicy.cpp)
    target_compile_features(ClawHUD.ProductionTargetPolicyTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.ProductionTargetPolicyTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.ProductionTargetPolicyTests PRIVATE user32)
    set_target_properties(ClawHUD.ProductionTargetPolicyTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ProductionTargetPolicyTests COMMAND ClawHUD.ProductionTargetPolicyTests)

    add_executable(ClawHUD.IntelVrrRangeTweakTests
        tests/IntelVrrRangeTweakTests.cpp
        src/ClawHUD/Tweaks/IntelVrr/AffectedPanelDetector.cpp
        src/ClawHUD/Tweaks/IntelVrr/IntelVrrRangeTweak.cpp
        src/ClawHUD/Tweaks/IntelVrr/IntelArcSyncClient.cpp
        src/ClawHUD/Tweaks/IntelVrr/IntelVrrResultStore.cpp
        src/ClawHUD/Tweaks/IntelVrr/IntelVrrRunResult.cpp)
    target_compile_features(ClawHUD.IntelVrrRangeTweakTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.IntelVrrRangeTweakTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.IntelVrrRangeTweakTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.IntelVrrRangeTweakTests PRIVATE wbemuuid ole32 oleaut32 shell32)
    set_target_properties(ClawHUD.IntelVrrRangeTweakTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.IntelVrrRangeTweakTests COMMAND ClawHUD.IntelVrrRangeTweakTests)

    add_executable(ClawHUD.TweakStartupCoordinatorTests tests/TweakStartupCoordinatorTests.cpp)
    target_compile_features(ClawHUD.TweakStartupCoordinatorTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.TweakStartupCoordinatorTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.TweakStartupCoordinatorTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.TweakStartupCoordinatorTests COMMAND ClawHUD.TweakStartupCoordinatorTests)

    add_executable(ClawHUD.SuspendResumeRecoveryTests tests/SuspendResumeRecoveryTests.cpp)
    target_compile_features(ClawHUD.SuspendResumeRecoveryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.SuspendResumeRecoveryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.SuspendResumeRecoveryTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.SuspendResumeRecoveryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.SuspendResumeRecoveryTests COMMAND ClawHUD.SuspendResumeRecoveryTests)

    add_executable(ClawHUD.EcHelperProtocolTests tests/EcHelperProtocolTests.cpp)
    target_compile_features(ClawHUD.EcHelperProtocolTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.EcHelperProtocolTests PRIVATE src/shared)
    set_target_properties(ClawHUD.EcHelperProtocolTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.EcHelperProtocolTests COMMAND ClawHUD.EcHelperProtocolTests)

    add_executable(ClawHUD.MsiEcHudTelemetryTests
        tests/MsiEcHudTelemetryTests.cpp
        src/ClawHUD/MsiEcHudTelemetry.cpp)
    target_compile_features(ClawHUD.MsiEcHudTelemetryTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.MsiEcHudTelemetryTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.MsiEcHudTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.MsiEcHudTelemetryTests COMMAND ClawHUD.MsiEcHudTelemetryTests)

    add_executable(ClawHUD.PresentMonFrameTelemetryTests
        tests/PresentMonFrameTelemetryTests.cpp
        src/ClawHUD/PresentMonFrameTelemetry.cpp
        src/ClawHUD/PresentMonApi2Client.cpp)
    target_compile_features(ClawHUD.PresentMonFrameTelemetryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.PresentMonFrameTelemetryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.PresentMonFrameTelemetryTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.PresentMonFrameTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.PresentMonFrameTelemetryTests COMMAND ClawHUD.PresentMonFrameTelemetryTests)

    add_executable(ClawHUD.WindowsPowerTelemetryTests
        tests/WindowsPowerTelemetryTests.cpp
        src/ClawHUD/WindowsPowerTelemetry.cpp
        src/ClawHUD/BatteryPowerEstimator.cpp
        src/ClawHUD/MsiEcHudTelemetry.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.WindowsPowerTelemetryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.WindowsPowerTelemetryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.WindowsPowerTelemetryTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.WindowsPowerTelemetryTests PRIVATE
        powrprof shell32 ole32)
    set_target_properties(ClawHUD.WindowsPowerTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.WindowsPowerTelemetryTests COMMAND ClawHUD.WindowsPowerTelemetryTests)

    add_executable(ClawHUD.WindowsMemoryTelemetryTests
        tests/WindowsMemoryTelemetryTests.cpp
        src/ClawHUD/WindowsMemoryTelemetry.cpp)
    target_compile_features(ClawHUD.WindowsMemoryTelemetryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.WindowsMemoryTelemetryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.WindowsMemoryTelemetryTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.WindowsMemoryTelemetryTests PRIVATE shell32)
    set_target_properties(ClawHUD.WindowsMemoryTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.WindowsMemoryTelemetryTests COMMAND ClawHUD.WindowsMemoryTelemetryTests)

    add_executable(ClawHUD.TelemetryRetentionTests
        tests/TelemetryRetentionTests.cpp)
    target_compile_features(ClawHUD.TelemetryRetentionTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.TelemetryRetentionTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.TelemetryRetentionTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.TelemetryRetentionTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.TelemetryRetentionTests COMMAND ClawHUD.TelemetryRetentionTests)

    add_executable(ClawHUD.IntelGraphicsApiProbeTests
        tests/IntelGraphicsApiProbeTests.cpp
        src/ClawHUD/IntelGraphicsApiProbe.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.IntelGraphicsApiProbeTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.IntelGraphicsApiProbeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.IntelGraphicsApiProbeTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.IntelGraphicsApiProbeTests PRIVATE shell32 ole32)
    set_target_properties(ClawHUD.IntelGraphicsApiProbeTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.IntelGraphicsApiProbeTests COMMAND ClawHUD.IntelGraphicsApiProbeTests)

    add_executable(ClawHUD.SupportedHardwareTests
        tests/SupportedHardwareTests.cpp
        src/ClawHUD/SupportedHardware.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.SupportedHardwareTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.SupportedHardwareTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.SupportedHardwareTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.SupportedHardwareTests PRIVATE wbemuuid ole32 oleaut32 shell32)
    set_target_properties(ClawHUD.SupportedHardwareTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.SupportedHardwareTests COMMAND ClawHUD.SupportedHardwareTests)

    add_executable(ClawHUD.HudSizeTests
        tests/HudSizeTests.cpp
        src/ClawHUD/HudSize.cpp)
    target_compile_features(ClawHUD.HudSizeTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.HudSizeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.HudSizeTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.HudSizeTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudSizeTests COMMAND ClawHUD.HudSizeTests)

    add_executable(ClawHUD.SettingsWindowGeometryTests
        tests/SettingsWindowGeometryTests.cpp
        src/ClawHUD/SettingsWindowGeometry.cpp)
    target_compile_features(ClawHUD.SettingsWindowGeometryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.SettingsWindowGeometryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.SettingsWindowGeometryTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.SettingsWindowGeometryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.SettingsWindowGeometryTests COMMAND ClawHUD.SettingsWindowGeometryTests)

    add_executable(ClawHUD.UninstallCleanupTests
        tests/UninstallCleanupTests.cpp
        src/ClawHUD/UninstallCleanup.cpp)
    target_compile_features(ClawHUD.UninstallCleanupTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.UninstallCleanupTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.UninstallCleanupTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.UninstallCleanupTests PRIVATE shell32 ole32)
    set_target_properties(ClawHUD.UninstallCleanupTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.UninstallCleanupTests COMMAND ClawHUD.UninstallCleanupTests)

    add_executable(ClawHUD.HudPresentationContractTests
        tests/HudPresentationContractTests.cpp
        src/ClawHUD/HudPresentationContract.cpp)
    target_compile_features(ClawHUD.HudPresentationContractTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.HudPresentationContractTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.HudPresentationContractTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.HudPresentationContractTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudPresentationContractTests COMMAND ClawHUD.HudPresentationContractTests)

    add_executable(ClawHUD.HudPresentationLifecycleTests
        tests/HudPresentationLifecycleTests.cpp
        src/ClawHUD/HudPresentationLifecycle.cpp)
    target_compile_features(ClawHUD.HudPresentationLifecycleTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.HudPresentationLifecycleTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.HudPresentationLifecycleTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudPresentationLifecycleTests COMMAND ClawHUD.HudPresentationLifecycleTests)

    add_executable(ClawHUD.SteamRunningAppIdSourceTests
        tests/SteamRunningAppIdSourceTests.cpp
        src/ClawHUD/SteamRunningAppIdSource.cpp)
    target_compile_features(ClawHUD.SteamRunningAppIdSourceTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.SteamRunningAppIdSourceTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.SteamRunningAppIdSourceTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.SteamRunningAppIdSourceTests PRIVATE advapi32 user32)
    set_target_properties(ClawHUD.SteamRunningAppIdSourceTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.SteamRunningAppIdSourceTests COMMAND ClawHUD.SteamRunningAppIdSourceTests)

    add_executable(ClawHUD.WindowsGameIdentitySourceTests
        tests/WindowsGameIdentitySourceTests.cpp
        src/ClawHUD/GameDetection/WindowsGameIdentitySource.cpp
        src/ClawHUD/GameDetection/WindowsGameIdentityProbe.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.WindowsGameIdentitySourceTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.WindowsGameIdentitySourceTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.WindowsGameIdentitySourceTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.WindowsGameIdentitySourceTests PRIVATE shell32 propsys ole32)
    set_target_properties(ClawHUD.WindowsGameIdentitySourceTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.WindowsGameIdentitySourceTests COMMAND ClawHUD.WindowsGameIdentitySourceTests)

    add_executable(ClawHUD.MicrosoftGameTriggerTests
        tests/MicrosoftGameTriggerTests.cpp
        src/ClawHUD/GameDetection/GameProcessInstance.cpp
        src/ClawHUD/GameDetection/KnownGameProcessCache.cpp
        src/ClawHUD/GameDetection/MicrosoftGameTrigger.cpp
        src/ClawHUD/GameDetection/WindowsGameIdentityProbe.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.MicrosoftGameTriggerTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.MicrosoftGameTriggerTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.MicrosoftGameTriggerTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.MicrosoftGameTriggerTests PRIVATE
        shell32 propsys ole32 user32)
    set_target_properties(ClawHUD.MicrosoftGameTriggerTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.MicrosoftGameTriggerTests
        COMMAND ClawHUD.MicrosoftGameTriggerTests)

    add_executable(ClawHUD.GameRenderVerifierTests
        tests/GameRenderVerifierTests.cpp
        src/ClawHUD/GameDetection/GameRenderVerifier.cpp
        src/ClawHUD/PresentMonTelemetryProvider.cpp
        src/ClawHUD/PresentMonFrameTelemetry.cpp
        src/ClawHUD/PresentMonDebugFrameTelemetry.cpp
        src/ClawHUD/PresentMonProcessTelemetry.cpp
        src/ClawHUD/PresentMonSystemTelemetry.cpp
        src/ClawHUD/PresentMonApi2Client.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.GameRenderVerifierTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.GameRenderVerifierTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.GameRenderVerifierTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.GameRenderVerifierTests PRIVATE
        advapi32 shell32 user32 ole32)
    set_target_properties(ClawHUD.GameRenderVerifierTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.GameRenderVerifierTests
        COMMAND ClawHUD.GameRenderVerifierTests)

    add_executable(ClawHUD.PresentActivitySourceTests
        tests/PresentActivitySourceTests.cpp
        src/ClawHUD/GameDetection/PresentActivitySource.cpp
        src/ClawHUD/PresentMonTelemetryProvider.cpp
        src/ClawHUD/PresentMonFrameTelemetry.cpp
        src/ClawHUD/PresentMonDebugFrameTelemetry.cpp
        src/ClawHUD/PresentMonProcessTelemetry.cpp
        src/ClawHUD/PresentMonSystemTelemetry.cpp
        src/ClawHUD/PresentMonApi2Client.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.PresentActivitySourceTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.PresentActivitySourceTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.PresentActivitySourceTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.PresentActivitySourceTests PRIVATE advapi32 shell32 ole32)
    set_target_properties(ClawHUD.PresentActivitySourceTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.PresentActivitySourceTests COMMAND ClawHUD.PresentActivitySourceTests)

    add_executable(ClawHUD.PresentMonDebugFrameTelemetryTests
        tests/PresentMonDebugFrameTelemetryTests.cpp
        src/ClawHUD/PresentMonDebugFrameTelemetry.cpp
        src/ClawHUD/PresentMonApi2Client.cpp)
    target_compile_features(ClawHUD.PresentMonDebugFrameTelemetryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.PresentMonDebugFrameTelemetryTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.PresentMonDebugFrameTelemetryTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.PresentMonDebugFrameTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.PresentMonDebugFrameTelemetryTests COMMAND ClawHUD.PresentMonDebugFrameTelemetryTests)

    add_executable(ClawHUD.WindowLifecycleSourceTests
        tests/WindowLifecycleSourceTests.cpp
        src/ClawHUD/GameDetection/WindowLifecycleSource.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.WindowLifecycleSourceTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.WindowLifecycleSourceTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.WindowLifecycleSourceTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.WindowLifecycleSourceTests PRIVATE
        dwmapi shell32 user32 ole32)
    set_target_properties(ClawHUD.WindowLifecycleSourceTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.WindowLifecycleSourceTests COMMAND ClawHUD.WindowLifecycleSourceTests)

    add_executable(ClawHUD.ProductionGameWindowSourceTests
        tests/ProductionGameWindowSourceTests.cpp
        src/ClawHUD/GameDetection/ProductionGameWindowSource.cpp)
    target_compile_features(ClawHUD.ProductionGameWindowSourceTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.ProductionGameWindowSourceTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.ProductionGameWindowSourceTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.ProductionGameWindowSourceTests PRIVATE user32)
    set_target_properties(ClawHUD.ProductionGameWindowSourceTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ProductionGameWindowSourceTests
        COMMAND ClawHUD.ProductionGameWindowSourceTests)

    add_executable(ClawHUD.ProcessLifecycleSourceTests
        tests/ProcessLifecycleSourceTests.cpp
        src/ClawHUD/GameDetection/ProcessLifecycleSource.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.ProcessLifecycleSourceTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.ProcessLifecycleSourceTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.ProcessLifecycleSourceTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.ProcessLifecycleSourceTests PRIVATE
        wbemuuid ole32 oleaut32 shell32)
    set_target_properties(ClawHUD.ProcessLifecycleSourceTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ProcessLifecycleSourceTests COMMAND ClawHUD.ProcessLifecycleSourceTests)

    add_executable(ClawHUD.PresentMonApi2ClientTests
        tests/PresentMonApi2ClientTests.cpp
        src/ClawHUD/PresentMonApi2Client.cpp)
    target_compile_features(ClawHUD.PresentMonApi2ClientTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.PresentMonApi2ClientTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.PresentMonApi2ClientTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.PresentMonApi2ClientTests PRIVATE kernel32)
    set_target_properties(ClawHUD.PresentMonApi2ClientTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.PresentMonApi2ClientTests COMMAND ClawHUD.PresentMonApi2ClientTests)

    add_executable(ClawHUD.PresentMonTelemetryProviderTests
        tests/PresentMonTelemetryProviderTests.cpp
        src/ClawHUD/PresentMonTelemetryProvider.cpp
        src/ClawHUD/PresentMonProcessTelemetry.cpp
        src/ClawHUD/PresentMonSystemTelemetry.cpp
        src/ClawHUD/PresentMonFrameTelemetry.cpp
        src/ClawHUD/PresentMonDebugFrameTelemetry.cpp
        src/ClawHUD/PresentMonApi2Client.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.PresentMonTelemetryProviderTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.PresentMonTelemetryProviderTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.PresentMonTelemetryProviderTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.PresentMonTelemetryProviderTests PRIVATE kernel32 shell32)
    set_target_properties(ClawHUD.PresentMonTelemetryProviderTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.PresentMonTelemetryProviderTests
        COMMAND ClawHUD.PresentMonTelemetryProviderTests)

    add_executable(ClawHUD.PresentMonRuntimeBootstrapTests
        tests/PresentMonRuntimeBootstrapTests.cpp
        src/ClawHUD/PresentMonRuntimeBootstrap.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.PresentMonRuntimeBootstrapTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.PresentMonRuntimeBootstrapTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.PresentMonRuntimeBootstrapTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.PresentMonRuntimeBootstrapTests PRIVATE
        shell32 advapi32 version ole32)
    set_target_properties(ClawHUD.PresentMonRuntimeBootstrapTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.PresentMonRuntimeBootstrapTests
        COMMAND ClawHUD.PresentMonRuntimeBootstrapTests)

# --- ClawHUD.Diag standalone diagnostic test targets ---------------------
# Added with the diagnostic foundation (PR #190); kept here so the root
# CMakeLists.txt stays production-focused (R8).
    add_executable(ClawHUD.DiagApi2EvidenceTests
        tests/DiagApi2EvidenceTests.cpp
        src/ClawHUD.Diag/Api2Evidence.cpp
        src/ClawHUD.Diag/DiagPresentMonApi2Client.cpp)
    target_compile_features(ClawHUD.DiagApi2EvidenceTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.DiagApi2EvidenceTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.DiagApi2EvidenceTests PRIVATE src/ClawHUD.Diag)
    target_link_libraries(ClawHUD.DiagApi2EvidenceTests PRIVATE kernel32)
    set_target_properties(ClawHUD.DiagApi2EvidenceTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.DiagApi2EvidenceTests COMMAND ClawHUD.DiagApi2EvidenceTests)

    add_executable(ClawHUD.DiagProcessMetadataTests
        tests/DiagProcessMetadataTests.cpp
        src/ClawHUD.Diag/DiagnosticSession.cpp
        src/ClawHUD.Diag/Api2Evidence.cpp
        src/ClawHUD.Diag/DiagPresentMonApi2Client.cpp)
    target_compile_features(ClawHUD.DiagProcessMetadataTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.DiagProcessMetadataTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.DiagProcessMetadataTests PRIVATE src/ClawHUD.Diag "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(ClawHUD.DiagProcessMetadataTests PRIVATE advapi32 dwmapi user32 pdh)
    set_target_properties(ClawHUD.DiagProcessMetadataTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.DiagProcessMetadataTests COMMAND ClawHUD.DiagProcessMetadataTests)

    add_executable(ClawHUD.DiagWinEventTests
        tests/DiagWinEventTests.cpp
        src/ClawHUD.Diag/DiagnosticSession.cpp
        src/ClawHUD.Diag/Api2Evidence.cpp
        src/ClawHUD.Diag/DiagPresentMonApi2Client.cpp)
    target_compile_features(ClawHUD.DiagWinEventTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.DiagWinEventTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.DiagWinEventTests PRIVATE src/ClawHUD.Diag "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(ClawHUD.DiagWinEventTests PRIVATE advapi32 dwmapi user32 pdh)
    set_target_properties(ClawHUD.DiagWinEventTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.DiagWinEventTests COMMAND ClawHUD.DiagWinEventTests)

    add_test(NAME ClawHUD.DiagJsonlSmoke
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_SOURCE_DIR}/tests/DiagJsonlSmoke.ps1" "$<TARGET_FILE:ClawHUD.Diag>")
    set_tests_properties(ClawHUD.DiagJsonlSmoke PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:ClawHUD.Diag>")
