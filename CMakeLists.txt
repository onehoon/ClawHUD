cmake_minimum_required(VERSION 3.25)
project(ClawHUD LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ClawHUD Phase 0 requires Windows")
endif()

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

add_library(ClawHUD.Velopack SHARED IMPORTED GLOBAL)
set_target_properties(ClawHUD.Velopack PROPERTIES
    IMPORTED_IMPLIB "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll.lib"
    IMPORTED_LOCATION "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${VELOPACK_ROOT}/include")

add_executable(ClawHUD WIN32
    src/ClawHUD/main.cpp
    src/ClawHUD/App.cpp
    src/ClawHUD/TrayIcon.cpp
    src/ClawHUD/SettingsWindow.cpp)
target_compile_features(ClawHUD PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    CLAWHUD_UPDATE_REPOSITORY=\"https://github.com/onehoon/ClawHUD\")
target_include_directories(ClawHUD PRIVATE src/ClawHUD)
target_link_libraries(ClawHUD PRIVATE ClawHUD.Velopack comctl32 shell32 delayimp)
target_link_options(ClawHUD PRIVATE "/DELAYLOAD:velopack_libc.dll")
set_target_properties(ClawHUD PROPERTIES CXX_EXTENSIONS OFF)
add_custom_command(TARGET ClawHUD POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll"
        "$<TARGET_FILE_DIR:ClawHUD>/velopack_libc.dll")
