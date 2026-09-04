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
set(PRESENTMON_API2_ROOT "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.5.1")

# One source of truth: the bundled runtime path, the readiness version floor, and
# the wrapper MSI ProductVersion all derive from PRESENTMON_VERSION.
if(NOT PRESENTMON_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "PRESENTMON_VERSION must be major.minor.patch")
endif()
set(PRESENTMON_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PRESENTMON_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PRESENTMON_VERSION_PATCH "${CMAKE_MATCH_3}")
configure_file(
    "${CMAKE_SOURCE_DIR}/src/ClawHUD/PresentMonRuntimeVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/PresentMonRuntimeVersion.h"
    @ONLY)

file(READ "${CMAKE_SOURCE_DIR}/archive/diagnostics/presentmon-api2/assets/PresentMonAutoTargetBlockList.txt"
    CLAWHUD_DIAG_BLOCKLIST)
file(WRITE "${CMAKE_BINARY_DIR}/generated/DiagBlocklist.h"
    "#pragma once\n#include <string_view>\nconstexpr std::string_view kDiagPresentMonBlocklist = R\"BLOCK(${CLAWHUD_DIAG_BLOCKLIST})BLOCK\";\n")

add_executable(ClawHUD.Diag
    src/ClawHUD.Diag/main.cpp
    src/ClawHUD.Diag/DiagnosticSession.cpp
    src/ClawHUD.Diag/Api2Evidence.cpp
    src/ClawHUD.Diag/DiagPresentMonApi2Client.cpp)
target_compile_features(ClawHUD.Diag PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD.Diag PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(ClawHUD.Diag PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(ClawHUD.Diag PRIVATE
    src/ClawHUD.Diag
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ClawHUD.Diag PRIVATE advapi32 dwmapi user32 pdh)

add_library(ClawHUD.Velopack SHARED IMPORTED GLOBAL)
set_target_properties(ClawHUD.Velopack PROPERTIES
    IMPORTED_IMPLIB "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll.lib"
    IMPORTED_LOCATION "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${VELOPACK_ROOT}/include")

add_executable(ClawHUD WIN32
    src/ClawHUD/main.cpp
    src/resources/ClawHUD.rc
    src/ClawHUD/App.cpp
    src/ClawHUD/ClawHudUpdateUrl.cpp
    src/ClawHUD/ClawHudUpdateSource.cpp
    src/ClawHUD/StartupExecutablePath.cpp
    src/ClawHUD/StartupTaskRegistration.cpp
    src/ClawHUD/LaunchMode.cpp
    src/ClawHUD/RuntimeControlDispatchBridge.cpp
    src/ClawHUD/RuntimeControlWireMapping.cpp
    src/ClawHUD/RuntimeControlPipeServer.cpp
    src/ClawHUD/RuntimeControlPipeSecurity.cpp
    src/shared/ClawHudControlCodec.cpp
    src/ClawHUD/ProductionTelemetryController.cpp
    src/ClawHUD/HudController.cpp
    src/ClawHUD/RuntimeLogger.cpp
    src/ClawHUD/Win32Format.cpp
    src/ClawHUD/ProcessLiveness.cpp
    src/ClawHUD/HudSettingsStore.cpp
    src/ClawHUD/HudTelemetryAggregator.cpp
    src/ClawHUD/GameDetection/GameSessionController.cpp
    src/ClawHUD/GameDetection/DebugObservationController.cpp
    src/ClawHUD/GameDetection/PresentActivitySource.cpp
    src/ClawHUD/GameDetection/WindowLifecycleSource.cpp
    src/ClawHUD/GameDetection/ProductionGameWindowSource.cpp
    src/ClawHUD/GameDetection/GameScreenAdmission.cpp
    src/ClawHUD/GameDetection/GameProcessInstance.cpp
    src/ClawHUD/GameDetection/KnownGameProcessCache.cpp
    src/ClawHUD/GameDetection/ForegroundGameDetector.cpp
    src/ClawHUD/TrayIcon.cpp
    src/ClawHUD/RuntimeMessageWindow.cpp
    src/ClawHUD/SettingsFrontendLauncher.cpp
    src/ClawHUD/PresentMonApi2Client.cpp
    src/ClawHUD/PresentMonTelemetryProvider.cpp
    src/ClawHUD/PresentMonProcessTelemetry.cpp
    src/ClawHUD/PresentMonDebugFrameTelemetry.cpp
    src/ClawHUD/AlwaysModeFpsTarget.cpp
    src/ClawHUD/FpsStaleHold.cpp
    src/ClawHUD/PresentMonSystemTelemetry.cpp
    src/ClawHUD/PresentMonRuntimeBootstrap.cpp
    src/ClawHUD/EcHelperClient.cpp
    src/ClawHUD/MsiEcHudTelemetry.cpp
    src/ClawHUD/PresentMonFrameTelemetry.cpp
    src/ClawHUD/WindowsPowerTelemetry.cpp
    src/ClawHUD/BatteryPowerEstimator.cpp
    src/ClawHUD/WindowsMemoryTelemetry.cpp
    src/ClawHUD/SupportedHardware.cpp
    src/ClawHUD/Tweaks/TweakStartupCoordinator.cpp
    src/ClawHUD/Tweaks/IntelVrr/AffectedPanelDetector.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelArcSyncClient.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelVrrRangeTweak.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelVrrRunResult.cpp
    src/ClawHUD/Tweaks/IntelVrr/IntelVrrResultStore.cpp
    src/ClawHUD/HudModel.cpp
    src/ClawHUD/HudSize.cpp
    src/ClawHUD/UninstallCleanup.cpp
    src/ClawHUD/HudRenderer.cpp
    src/ClawHUD/HudPresentation.cpp
    src/ClawHUD/HudWindowGeometry.cpp
    src/ClawHUD/HudPresentationContract.cpp
    src/ClawHUD/HudPresentationLifecycle.cpp
    src/ClawHUD/ForegroundTracker.cpp
    src/ClawHUD/GameDetection/WindowsGameIdentitySource.cpp
    src/ClawHUD/GameDetection/WindowsGameIdentityProbe.cpp
    src/ClawHUD/GameDetection/MicrosoftGameTrigger.cpp
    src/ClawHUD/GameDetection/GameRenderVerifier.cpp
    src/ClawHUD/ProductionTargetPolicy.cpp
    src/ClawHUD/SteamRunningAppIdSource.cpp)
target_compile_features(ClawHUD PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    CLAWHUD_UPDATE_REPOSITORY=\"https://github.com/onehoon/ClawHUD\")
target_include_directories(ClawHUD PRIVATE src/ClawHUD src/shared src/resources "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ClawHUD PRIVATE ClawHUD.Velopack comctl32 shell32 user32 winmm dwmapi delayimp d3d11 dxgi dcomp d2d1 dwrite powrprof advapi32 wbemuuid ole32 oleaut32 propsys winhttp)
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
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ClawHUD>/fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ClawHUD>/runtime"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PRESENTMON_API2_ROOT}/PresentMonAPI2Loader.dll"
        "$<TARGET_FILE_DIR:ClawHUD>/PresentMonAPI2Loader.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PRESENTMON_API2_ROOT}/ClawHUD.PresentMonRuntime.msi"
        "$<TARGET_FILE_DIR:ClawHUD>/runtime/ClawHUD.PresentMonRuntime.msi"
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
    add_executable(ClawHUD.GameSessionCutoverPolicyTests
        tests/GameSessionCutoverPolicyTests.cpp)
    target_compile_features(ClawHUD.GameSessionCutoverPolicyTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.GameSessionCutoverPolicyTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.GameSessionCutoverPolicyTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.GameSessionCutoverPolicyTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.GameSessionCutoverPolicyTests COMMAND ClawHUD.GameSessionCutoverPolicyTests)

    add_executable(ClawHUD.ForegroundGameDetectorTests
        tests/ForegroundGameDetectorTests.cpp
        src/ClawHUD/GameDetection/ForegroundGameDetector.cpp
        src/ClawHUD/GameDetection/GameScreenAdmission.cpp
        src/ClawHUD/GameDetection/GameProcessInstance.cpp
        src/ClawHUD/GameDetection/KnownGameProcessCache.cpp
        src/ClawHUD/ProductionTargetPolicy.cpp)
    target_compile_features(ClawHUD.ForegroundGameDetectorTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.ForegroundGameDetectorTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.ForegroundGameDetectorTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.ForegroundGameDetectorTests PRIVATE dwmapi user32)
    set_target_properties(ClawHUD.ForegroundGameDetectorTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.ForegroundGameDetectorTests COMMAND ClawHUD.ForegroundGameDetectorTests)

    add_executable(ClawHUD.KnownGameProcessCacheTests
        tests/KnownGameProcessCacheTests.cpp
        src/ClawHUD/GameDetection/GameProcessInstance.cpp
        src/ClawHUD/GameDetection/KnownGameProcessCache.cpp)
    target_compile_features(ClawHUD.KnownGameProcessCacheTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.KnownGameProcessCacheTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.KnownGameProcessCacheTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.KnownGameProcessCacheTests PRIVATE user32)
    set_target_properties(ClawHUD.KnownGameProcessCacheTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.KnownGameProcessCacheTests COMMAND ClawHUD.KnownGameProcessCacheTests)

    add_executable(ClawHUD.GameScreenAdmissionTests
        tests/GameScreenAdmissionTests.cpp
        src/ClawHUD/GameDetection/GameScreenAdmission.cpp
        src/ClawHUD/ProductionTargetPolicy.cpp)
    target_compile_features(ClawHUD.GameScreenAdmissionTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.GameScreenAdmissionTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.GameScreenAdmissionTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.GameScreenAdmissionTests PRIVATE dwmapi user32)
    set_target_properties(ClawHUD.GameScreenAdmissionTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.GameScreenAdmissionTests COMMAND ClawHUD.GameScreenAdmissionTests)

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ClawHUDTests.cmake")
endif()
