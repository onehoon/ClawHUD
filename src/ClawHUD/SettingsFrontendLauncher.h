#pragma once

#include <filesystem>

namespace clawhud
{
// The WPF Settings frontend (ClawHUD.Settings.exe) always ships beside
// ClawHUD.exe. Resolving it as a sibling of the running runtime executable keeps
// it correct across VeloPack `current` version swaps and portable extraction,
// independent of %PATH% or the caller's working directory.
std::filesystem::path SettingsFrontendPath(
    const std::filesystem::path& runtimeExecutable);

// Launches the sibling ClawHUD.Settings.exe unelevated and returns immediately;
// the runtime never waits for Settings to close. Returns false (and logs, plus
// shows a small error box for a missing frontend) without crashing so the
// runtime/HUD keep running. Never elevates and never starts a second ClawHUD.exe.
bool LaunchSettingsFrontend(const std::filesystem::path& runtimeExecutable);
}
