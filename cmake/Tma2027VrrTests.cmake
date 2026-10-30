add_executable(ClawHUD.Tma2027VrrPolicyTests
    tests/Tma2027VrrPolicyTests.cpp
    src/ClawHUD/Tweaks/IntelVrr/Tma2027VrrPolicy.cpp
    src/ClawHUD/Tweaks/IntelVrr/Tma2027VrrEvidence.cpp)
target_compile_features(ClawHUD.Tma2027VrrPolicyTests PRIVATE cxx_std_20)
target_compile_definitions(ClawHUD.Tma2027VrrPolicyTests PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(ClawHUD.Tma2027VrrPolicyTests PRIVATE
    src/ClawHUD)
target_link_libraries(ClawHUD.Tma2027VrrPolicyTests PRIVATE
    advapi32 bcrypt gdi32)
set_target_properties(ClawHUD.Tma2027VrrPolicyTests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME ClawHUD.Tma2027VrrPolicyTests COMMAND ClawHUD.Tma2027VrrPolicyTests)
