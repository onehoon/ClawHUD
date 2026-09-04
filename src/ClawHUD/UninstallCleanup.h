#pragma once

namespace clawhud
{
// Removes the owned "ClawHUD" Task Scheduler startup task, if present, using
// the same bounded self-elevated remove path as SetStartWithWindows(false)
// (see StartupTaskRegistration.h). Called from the VeloPack OnBeforeUninstall
// hook; must never throw and must never block uninstall indefinitely -- a
// cancelled/failed elevation is a best-effort failure, not a crash.
void CleanupForUninstall() noexcept;
}
