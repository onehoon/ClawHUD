cmake_minimum_required(VERSION 3.25)
project(ClawHUD LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ClawHUD requires Windows")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "ClawHUD requires MSVC and the Windows SDK")
endif()

set(CLAWHUD_VERSION "0.1.0" CACHE STRING "ClawHUD package and displayed application version")
if(NOT CLAWHUD_VERSION MATCHES "^0\\.1\\.(0|[1-9][0-9]?[0-9]?)$")
    message(FATAL_ERROR "CLAWHUD_VERSION must match 0.1.0 through 0.1.999")
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_SOURCE_DIR}/src/ClawHUD/Version.h.in"
    "${CMAKE_BINARY_DIR}/generated/Version.h"
    @ONLY)

add_executable(ClawHUD.VrrPoc WIN32 src/ClawHUD.VrrPoc/main.cpp)
target_compile_features(ClawHUD.VrrPoc PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD.VrrPoc PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ClawHUD.VrrPoc PRIVATE d3d11 dxgi dcomp d2d1 dwrite)
set_target_properties(ClawHUD.VrrPoc PROPERTIES CXX_EXTENSIONS OFF)

set(VELOPACK_VERSION "1.2.0")
set(VELOPACK_ARCHIVE "${CMAKE_BINARY_DIR}/_deps/velopack_libc_${VELOPACK_VERSION}.zip")
set(VELOPACK_ROOT "${CMAKE_BINARY_DIR}/_deps/velopack_libc_${VELOPACK_VERSION}")
file(DOWNLOAD
    "https://github.com/velopack/velopack/releases/download/${VELOPACK_VERSION}/velopack_libc_${VELOPACK_VERSION}.zip"
    "${VELOPACK_ARCHIVE}"
    EXPECTED_HASH SHA256=547262ED7A1AB1FF62F580AA53851EDE2F1A451AC61B8974EB7BC01117488835
    # The archive hash is pinned because some Windows CMake installations lack a current CA bundle.
    TLS_VERIFY OFF)
file(ARCHIVE_EXTRACT INPUT "${VELOPACK_ARCHIVE}" DESTINATION "${VELOPACK_ROOT}")

set(PRESENTMON_VERSION "2.5.1")
set(PRESENTMON_EXE "${CMAKE_BINARY_DIR}/_deps/PresentMon-${PRESENTMON_VERSION}-x64.exe")
set(PRESENTMON_LICENSE "${CMAKE_SOURCE_DIR}/third_party/PresentMon-LICENSE.txt")
file(DOWNLOAD
    "https://github.com/GameTechDev/PresentMon/releases/download/v${PRESENTMON_VERSION}/PresentMon-${PRESENTMON_VERSION}-x64.exe"
    "${PRESENTMON_EXE}"
    EXPECTED_HASH SHA256=9BEC3083069F58F911E6A512F4806DB51A27BD096103087BC1D05EF54C80A191
    TLS_VERIFY OFF)

add_library(ClawHUD.Velopack SHARED IMPORTED GLOBAL)
set_target_properties(ClawHUD.Velopack PROPERTIES
    IMPORTED_IMPLIB "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll.lib"
    IMPORTED_LOCATION "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${VELOPACK_ROOT}/include")

add_executable(ClawHUD WIN32
    src/ClawHUD/main.cpp
    src/resources/ClawHUD.rc
    src/ClawHUD/App.cpp
    src/ClawHUD/RuntimeLogger.cpp
    src/ClawHUD/TrayIcon.cpp
    src/ClawHUD/SettingsWindow.cpp
    src/ClawHUD/EcDiagnostic.cpp
    src/ClawHUD/EcHelperClient.cpp
    src/ClawHUD/MsiEcHudTelemetry.cpp
    src/ClawHUD/PresentMonHudTelemetry.cpp
    src/ClawHUD/WindowsPowerTelemetry.cpp
    src/ClawHUD/WindowsUsageTelemetry.cpp
    src/ClawHUD/IntelGraphicsApiProbe.cpp
    src/ClawHUD/SupportedHardware.cpp
    src/ClawHUD/VrrDiagnostic.cpp
    src/ClawHUD/VrrDiagnosticAnalysis.cpp
    src/ClawHUD/IntelVrrDiagnosticProbe.cpp
    src/ClawHUD/Tweaks/TweakStartupCoordinator.cpp
    src/ClawHUD/Tweaks/IntelVrr/AffectedPanelDetector.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelArcSyncClient.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelVrrRangeTweak.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelVrrRunResult.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelVrrResultStore.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelVrrRunLogger.cpp
    src/ClawHUD/HudModel.cpp
    src/ClawHUD/HudSize.cpp
    src/ClawHUD/UninstallCleanup.cpp
    src/ClawHUD/HudRenderer.cpp
    src/ClawHUD/HudPresentation.cpp
    src/ClawHUD/HudWindowGeometry.cpp
    src/ClawHUD/HudPresentationContract.cpp
    src/ClawHUD/HudPresentationLifecycle.cpp
    src/ClawHUD/ForegroundTracker.cpp
    src/ClawHUD/ProductionTargetPolicy.cpp)
target_compile_features(ClawHUD PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    CLAWHUD_UPDATE_REPOSITORY=\"https://github.com/onehoon/ClawHUD\")
target_include_directories(ClawHUD PRIVATE src/ClawHUD src/resources "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ClawHUD PRIVATE ClawHUD.Velopack comctl32 shell32 user32 winmm dwmapi delayimp d3d11 dxgi dcomp d2d1 dwrite pdh advapi32 wbemuuid ole32 oleaut32)
target_link_options(ClawHUD PRIVATE "/DELAYLOAD:velopack_libc.dll")
set_target_properties(ClawHUD PROPERTIES CXX_EXTENSIONS OFF)

add_executable(ClawHUD.EcHelper WIN32
    src/ClawHUD.EcHelper/main.cpp
    src/resources/ClawHUD.rc
    src/ClawHUD.EcHelper/MsiEcReader.cpp)
target_compile_features(ClawHUD.EcHelper PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD.EcHelper PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(ClawHUD.EcHelper PRIVATE src/ClawHUD.EcHelper src/shared src/resources)
target_link_libraries(ClawHUD.EcHelper PRIVATE wbemuuid ole32 oleaut32)
set_target_properties(ClawHUD.EcHelper PROPERTIES CXX_EXTENSIONS OFF)
add_custom_command(TARGET ClawHUD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ClawHUD>/tools"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ClawHUD>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PRESENTMON_EXE}"
        "$<TARGET_FILE_DIR:ClawHUD>/tools/PresentMon.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PRESENTMON_LICENSE}"
        "$<TARGET_FILE_DIR:ClawHUD>/tools/PresentMon-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/unispace/Unispace.otf"
        "$<TARGET_FILE_DIR:ClawHUD>/fonts/Unispace.otf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/unispace/Unispace-LICENSE.txt"
        "$<TARGET_FILE_DIR:ClawHUD>/fonts/Unispace-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll"
        "$<TARGET_FILE_DIR:ClawHUD>/velopack_libc.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ClawHUD.EcHelper>"
        "$<TARGET_FILE_DIR:ClawHUD>/ClawHUD.EcHelper.exe")

include(CTest)
if(BUILD_TESTING)
    add_executable(ClawHUD.HudModelTests
        tests/HudModelTests.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.HudModelTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.HudModelTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.HudModelTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudModelTests COMMAND ClawHUD.HudModelTests)

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

    add_executable(ClawHUD.ForegroundTrackerTests
        tests/ForegroundTrackerTests.cpp
        src/ClawHUD/ForegroundTracker.cpp)
    target_compile_features(ClawHUD.ForegroundTrackerTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.ForegroundTrackerTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.ForegroundTrackerTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.ForegroundTrackerTests PRIVATE user32)
    set_target_properties(ClawHUD.ForegroundTrackerTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ForegroundTrackerTests COMMAND ClawHUD.ForegroundTrackerTests)

    add_executable(ClawHUD.ProductionTargetPolicyTests
        tests/ProductionTargetPolicyTests.cpp
        src/ClawHUD/ProductionTargetPolicy.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.ProductionTargetPolicyTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.ProductionTargetPolicyTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.ProductionTargetPolicyTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ProductionTargetPolicyTests COMMAND ClawHUD.ProductionTargetPolicyTests)

    add_executable(ClawHUD.IntelVrrDiagnosticProbeTests
        tests/IntelVrrDiagnosticProbeTests.cpp
        src/ClawHUD/IntelVrrDiagnosticProbe.cpp)
    target_compile_features(ClawHUD.IntelVrrDiagnosticProbeTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.IntelVrrDiagnosticProbeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.IntelVrrDiagnosticProbeTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.IntelVrrDiagnosticProbeTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.IntelVrrDiagnosticProbeTests COMMAND ClawHUD.IntelVrrDiagnosticProbeTests)

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

    add_executable(ClawHUD.DiagnosticHudModeTests tests/DiagnosticHudModeTests.cpp)
    target_compile_features(ClawHUD.DiagnosticHudModeTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.DiagnosticHudModeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.DiagnosticHudModeTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.DiagnosticHudModeTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.DiagnosticHudModeTests COMMAND ClawHUD.DiagnosticHudModeTests)

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

    add_executable(ClawHUD.PresentMonHudTelemetryTests
        tests/PresentMonHudTelemetryTests.cpp
        src/ClawHUD/PresentMonHudTelemetry.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.PresentMonHudTelemetryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.PresentMonHudTelemetryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.PresentMonHudTelemetryTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.PresentMonHudTelemetryTests PRIVATE advapi32 shell32 ole32)
    set_target_properties(ClawHUD.PresentMonHudTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.PresentMonHudTelemetryTests COMMAND ClawHUD.PresentMonHudTelemetryTests)

    add_executable(ClawHUD.WindowsPowerTelemetryTests
        tests/WindowsPowerTelemetryTests.cpp
        src/ClawHUD/WindowsPowerTelemetry.cpp)
    target_compile_features(ClawHUD.WindowsPowerTelemetryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.WindowsPowerTelemetryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.WindowsPowerTelemetryTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.WindowsPowerTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.WindowsPowerTelemetryTests COMMAND ClawHUD.WindowsPowerTelemetryTests)

    add_executable(ClawHUD.WindowsUsageTelemetryTests
        tests/WindowsUsageTelemetryTests.cpp
        src/ClawHUD/WindowsUsageTelemetry.cpp
        src/ClawHUD/RuntimeLogger.cpp)
    target_compile_features(ClawHUD.WindowsUsageTelemetryTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.WindowsUsageTelemetryTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.WindowsUsageTelemetryTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.WindowsUsageTelemetryTests PRIVATE pdh dxgi shell32 ole32)
    set_target_properties(ClawHUD.WindowsUsageTelemetryTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.WindowsUsageTelemetryTests COMMAND ClawHUD.WindowsUsageTelemetryTests)

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

    add_executable(ClawHUD.UninstallCleanupTests
        tests/UninstallCleanupTests.cpp
        src/ClawHUD/UninstallCleanup.cpp)
    target_compile_features(ClawHUD.UninstallCleanupTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.UninstallCleanupTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.UninstallCleanupTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.UninstallCleanupTests PRIVATE shell32 ole32)
    set_target_properties(ClawHUD.UninstallCleanupTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.UninstallCleanupTests COMMAND ClawHUD.UninstallCleanupTests)

    add_executable(ClawHUD.VrrDiagnosticAnalysisTests
        tests/VrrDiagnosticAnalysisTests.cpp
        src/ClawHUD/VrrDiagnosticAnalysis.cpp)
    target_compile_features(ClawHUD.VrrDiagnosticAnalysisTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.VrrDiagnosticAnalysisTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.VrrDiagnosticAnalysisTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.VrrDiagnosticAnalysisTests COMMAND ClawHUD.VrrDiagnosticAnalysisTests)

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
endif()
