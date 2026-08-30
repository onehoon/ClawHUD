#include "PresentMonRuntimeBootstrap.h"

#include "PresentMonApi2Api.h"
#include "RuntimeLogger.h"

#include <shellapi.h>
#include <windows.h>

#include <filesystem>
#include <string>

#pragma comment(lib, "version.lib")

namespace clawhud
{
namespace
{
using GetVersion = PM_STATUS(__cdecl*)(PM_VERSION*);

void Log(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Info,
        L"[PresentMonRuntime] " + message);
}

std::filesystem::path ModulePath()
{
    wchar_t path[MAX_PATH]{};
    const DWORD chars = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (chars == 0 || chars >= ARRAYSIZE(path)) return {};
    return path;
}

std::wstring RegistryMiddlewarePath()
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\INTEL\\PresentMon\\Service", 0,
        KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};
    wchar_t value[1024]{};
    DWORD size = sizeof(value);
    DWORD type{};
    const auto result = RegQueryValueExW(key, L"sharedMiddlewarePath", nullptr,
        &type, reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ ? value : L"";
}

bool ServiceRunning()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, L"PresentMonSharedService",
        SERVICE_QUERY_STATUS);
    if (!service)
    {
        CloseServiceHandle(manager);
        return false;
    }
    SERVICE_STATUS_PROCESS status{};
    DWORD size{};
    const bool queried = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<BYTE*>(&status), sizeof(status), &size) != FALSE;
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return queried && status.dwCurrentState == SERVICE_RUNNING;
}

bool MiddlewareCompatible(const std::filesystem::path& path)
{
    HMODULE middleware = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!middleware) return false;
    const auto getVersion = reinterpret_cast<GetVersion>(
        GetProcAddress(middleware, "pmGetApiVersion"));
    PM_VERSION version{};
    const bool compatible = getVersion &&
        getVersion(&version) == PM_STATUS_SUCCESS &&
        version.major == PM_API_VERSION_MAJOR &&
        version.minor == PM_API_VERSION_MINOR;
    FreeLibrary(middleware);
    return compatible;
}

PresentMonRuntimeReadinessEvidence ReadinessEvidence()
{
    const auto registryPath = RegistryMiddlewarePath();
    const std::filesystem::path middleware(registryPath);
    PresentMonRuntimeReadinessEvidence evidence{
        ServiceRunning(),
        !registryPath.empty(),
        !middleware.empty() && std::filesystem::exists(middleware),
        !middleware.empty() && middleware.filename() == L"PresentMonAPI2.dll",
        !middleware.empty() && MiddlewareCompatible(middleware),
    };
    return evidence;
}

bool RunInstaller(const std::filesystem::path& msi, DWORD& exitCode) noexcept
{
    std::wstring arguments = L"/i \"" + msi.wstring() + L"\" /qn /norestart";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = L"msiexec.exe";
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return false;

    const DWORD wait = WaitForSingleObject(info.hProcess, 5 * 60 * 1000);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(info.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(info.hProcess, 5000);
        CloseHandle(info.hProcess);
        SetLastError(ERROR_TIMEOUT);
        return false;
    }
    const bool read = GetExitCodeProcess(info.hProcess, &exitCode) != FALSE;
    CloseHandle(info.hProcess);
    return read;
}
}

std::filesystem::path PresentMonRuntimeMsiPathForModule(
    const std::filesystem::path& modulePath)
{
    if (modulePath.empty()) return {};
    return modulePath.parent_path() / L"runtime" /
        L"ClawHUD.PresentMonRuntime.msi";
}

bool IsPresentMonRuntimeReady(
    const PresentMonRuntimeReadinessEvidence& evidence) noexcept
{
    return evidence.serviceRunning && evidence.registryPathPresent &&
        evidence.middlewareExists && evidence.middlewareNameValid &&
        evidence.compatible;
}

bool IsPresentMonRuntimeReady() noexcept
{
    try { return IsPresentMonRuntimeReady(ReadinessEvidence()); }
    catch (...) { return false; }
}

PresentMonRuntimeMsiExit ClassifyPresentMonRuntimeMsiExit(
    DWORD exitCode) noexcept
{
    if (exitCode == ERROR_SUCCESS)
        return PresentMonRuntimeMsiExit::SuccessCandidate;
    if (exitCode == ERROR_SUCCESS_REBOOT_REQUIRED)
        return PresentMonRuntimeMsiExit::RebootRequiredCandidate;
    return PresentMonRuntimeMsiExit::Failed;
}

PresentMonRuntimeBootstrapResult EnsurePresentMonRuntime() noexcept
{
    try
    {
        if (IsPresentMonRuntimeReady())
        {
            Log(L"state=ready action=none");
            return PresentMonRuntimeBootstrapResult::AlreadyReady;
        }
        Log(L"state=missing action=install");
        const auto msi = PresentMonRuntimeMsiPathForModule(ModulePath());
        Log(L"msi=" + msi.wstring());
        if (msi.empty() || !std::filesystem::exists(msi))
        {
            Log(L"validation=failed reason=msi_missing");
            return PresentMonRuntimeBootstrapResult::MsiMissing;
        }

        Log(L"elevation=requested");
        DWORD exitCode{};
        if (!RunInstaller(msi, exitCode))
        {
            const auto error = GetLastError();
            if (error == ERROR_CANCELLED)
            {
                Log(L"elevation=cancelled");
                return PresentMonRuntimeBootstrapResult::ElevationCancelled;
            }
            Log(L"installer_exit=unavailable validation=failed");
            return PresentMonRuntimeBootstrapResult::InstallFailed;
        }
        Log(L"installer_exit=" + std::to_wstring(exitCode));
        const auto classification = ClassifyPresentMonRuntimeMsiExit(exitCode);
        if (classification == PresentMonRuntimeMsiExit::Failed)
        {
            Log(L"validation=failed");
            return PresentMonRuntimeBootstrapResult::InstallFailed;
        }
        if (!IsPresentMonRuntimeReady())
        {
            Log(L"validation=failed");
            return PresentMonRuntimeBootstrapResult::ValidationFailed;
        }
        Log(L"validation=ready");
        return classification == PresentMonRuntimeMsiExit::RebootRequiredCandidate
            ? PresentMonRuntimeBootstrapResult::InstalledRebootRequired
            : PresentMonRuntimeBootstrapResult::Installed;
    }
    catch (...)
    {
        Log(L"validation=failed reason=exception");
        return PresentMonRuntimeBootstrapResult::InstallFailed;
    }
}
}
