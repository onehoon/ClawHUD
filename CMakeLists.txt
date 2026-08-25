cmake_minimum_required(VERSION 3.25)
project(ClawHUD LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ClawHUD Phase 0 requires Windows")
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
    src/ClawHUD/App.cpp
    src/ClawHUD/TrayIcon.cpp
    src/ClawHUD/SettingsWindow.cpp
    src/ClawHUD/EcDiagnostic.cpp
    src/ClawHUD/MsiEcReader.cpp
    src/ClawHUD/VrrDiagnostic.cpp
    src/ClawHUD/IntelVrrDiagnosticProbe.cpp
    src/ClawHUD/HudModel.cpp
    src/ClawHUD/HudRenderer.cpp
    src/ClawHUD/HudPresentation.cpp
    src/ClawHUD/ForegroundTracker.cpp)
target_compile_features(ClawHUD PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    CLAWHUD_UPDATE_REPOSITORY=\"https://github.com/onehoon/ClawHUD\")
target_include_directories(ClawHUD PRIVATE src/ClawHUD "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ClawHUD PRIVATE ClawHUD.Velopack comctl32 shell32 delayimp d3d11 dxgi dcomp d2d1 dwrite)
target_link_options(ClawHUD PRIVATE "/DELAYLOAD:velopack_libc.dll")
set_target_properties(ClawHUD PROPERTIES CXX_EXTENSIONS OFF)
add_custom_command(TARGET ClawHUD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ClawHUD>/tools"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PRESENTMON_EXE}"
        "$<TARGET_FILE_DIR:ClawHUD>/tools/PresentMon.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PRESENTMON_LICENSE}"
        "$<TARGET_FILE_DIR:ClawHUD>/tools/PresentMon-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll"
        "$<TARGET_FILE_DIR:ClawHUD>/velopack_libc.dll")

include(CTest)
if(BUILD_TESTING)
    add_executable(ClawHUD.HudModelTests
        tests/HudModelTests.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.HudModelTests PRIVATE cxx_std_20)
    target_include_directories(ClawHUD.HudModelTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.HudModelTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.HudModelTests COMMAND ClawHUD.HudModelTests)

    add_executable(ClawHUD.HudRendererTests
        tests/HudRendererTests.cpp
        src/ClawHUD/HudRenderer.cpp
        src/ClawHUD/HudModel.cpp)
    target_compile_features(ClawHUD.HudRendererTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.HudRendererTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.HudRendererTests PRIVATE src/ClawHUD)
    target_link_libraries(ClawHUD.HudRendererTests PRIVATE d2d1 dwrite)
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

    add_executable(ClawHUD.IntelVrrDiagnosticProbeTests
        tests/IntelVrrDiagnosticProbeTests.cpp
        src/ClawHUD/IntelVrrDiagnosticProbe.cpp)
    target_compile_features(ClawHUD.IntelVrrDiagnosticProbeTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.IntelVrrDiagnosticProbeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.IntelVrrDiagnosticProbeTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.IntelVrrDiagnosticProbeTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.IntelVrrDiagnosticProbeTests COMMAND ClawHUD.IntelVrrDiagnosticProbeTests)

    add_executable(ClawHUD.DiagnosticHudModeTests tests/DiagnosticHudModeTests.cpp)
    target_compile_features(ClawHUD.DiagnosticHudModeTests PRIVATE cxx_std_20)
    target_compile_definitions(ClawHUD.DiagnosticHudModeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ClawHUD.DiagnosticHudModeTests PRIVATE src/ClawHUD)
    set_target_properties(ClawHUD.DiagnosticHudModeTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME ClawHUD.DiagnosticHudModeTests COMMAND ClawHUD.DiagnosticHudModeTests)
endif()
