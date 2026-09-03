#pragma once

#include <filesystem>
#include <string>

namespace clawhud
{
// Which executable a Start-with-Windows shortcut should target.
struct StartupExecutable
{
    std::filesystem::path path;
    // true  -> <RootAppDir>\ClawHUD.exe, the stable VeloPack execution stub that
    //          survives `current` replacement across updates.
    // false -> the running process executable (portable / dev / unexpected
    //          layout); `fallbackReason` says why.
    bool velopackRootStub{};
    std::wstring fallbackReason;
};

// Resolves the stable startup target from the running process path. Selects the
// VeloPack root stub only when the full installed layout is present:
//   <root>\current\ClawHUD.exe   (processExecutable, parent dir named "current")
//   <root>\Update.exe
//   <root>\ClawHUD.exe
//   <root>\current\sq.version
// Any missing piece falls back to `processExecutable` -- Start-with-Windows must
// never fail just because ClawHUD is not in a normal installed layout.
StartupExecutable ResolveStartupExecutable(
    const std::filesystem::path& processExecutable);
}
