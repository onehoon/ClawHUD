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
