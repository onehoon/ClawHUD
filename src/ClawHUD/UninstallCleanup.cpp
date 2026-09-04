#include "UninstallCleanup.h"

#include "StartupTaskRegistration.h"

#include <windows.h>

namespace clawhud
{
void CleanupForUninstall() noexcept
{
    try
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (length == 0 || length >= ARRAYSIZE(path))
            return;
        SynchronizeStartupTask(false, path);
    }
    catch (...)
    {
    }
}
}
