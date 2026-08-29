#include "WindowsGameIdentitySource.h"

#include "RuntimeLogger.h"

#include <appmodel.h>
#include <shtypes.h>
#include <propkey.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <array>
#include <sstream>

#pragma comment(lib, "propsys.lib")

namespace clawhud
{
std::wstring EscapeWindowsIdentityDiagnosticValue(std::wstring_view value)
{
    std::wstring result;
    result.reserve(value.size());
    for (const wchar_t character : value)
    {
        if (character == L'\\') result += L"\\\\";
        else if (character == L'\"') result += L"\\\"";
        else if (character == L'\r') result += L"\\r";
        else if (character == L'\n') result += L"\\n";
        else if (character == L'\t') result += L"\\t";
        else result += character;
    }
    return result;
}

namespace
{
std::wstring ErrorCode(DWORD error) { return std::to_wstring(error); }

std::wstring ResultName(LONG result)
{
    if (result == ERROR_SUCCESS) return L"SUCCESS";
    if (result == APPMODEL_ERROR_NO_PACKAGE) return L"NO_PACKAGE";
    if (result == APPMODEL_ERROR_NO_APPLICATION) return L"NOT_PRESENT";
    if (result == ERROR_PROC_NOT_FOUND) return L"SYMBOL_MISSING";
    if (result == ERROR_ACCESS_DENIED) return L"ACCESS_DENIED";
    if (result == ERROR_INSUFFICIENT_BUFFER) return L"BUFFER_REQUIRED";
    if (result == ERROR_NOT_FOUND || result == ERROR_FILE_NOT_FOUND ||
        result == ERROR_PATH_NOT_FOUND) return L"NOT_PRESENT";
    if (result == ERROR_NOT_SUPPORTED || result == ERROR_CALL_NOT_IMPLEMENTED)
        return L"NOT_SUPPORTED";
    return L"API_FAILED";
}

void Debug(const std::wstring& message)
{
    RuntimeLogger::Log(RuntimeLogLevel::Debug, L"[GameIdentity] " + message);
}

std::wstring Escape(std::wstring_view value)
{
    return EscapeWindowsIdentityDiagnosticValue(value);
}

struct ScopedComApartment
{
    HRESULT hr{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)};
    ~ScopedComApartment()
    {
        if (hr == S_OK || hr == S_FALSE) CoUninitialize();
    }
    bool usable() const noexcept { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

struct UniquePropertyStore
{
    IPropertyStore* value{};
    ~UniquePropertyStore() { if (value) value->Release(); }
};

struct ScopedPropVariant
{
    PROPVARIANT value{};
    ScopedPropVariant() { PropVariantInit(&value); }
    ~ScopedPropVariant() { PropVariantClear(&value); }
};

std::wstring ReadWindowAumid(HWND window)
{
    ScopedComApartment com;
    if (!com.usable())
    {
        Debug(L"windowAumid.result=API_FAILED hr=" + std::to_wstring(com.hr));
        return {};
    }
    UniquePropertyStore store;
    const HRESULT storeResult = SHGetPropertyStoreForWindow(window,
        IID_PPV_ARGS(&store.value));
    if (FAILED(storeResult))
    {
        Debug(L"windowAumid.result=API_FAILED hr=" + std::to_wstring(storeResult));
        return {};
    }
    ScopedPropVariant value;
    const HRESULT result = store.value->GetValue(PKEY_AppUserModel_ID, &value.value);
    std::wstring text;
    if (SUCCEEDED(result) && value.value.vt == VT_LPWSTR && value.value.pwszVal)
        text = value.value.pwszVal;
    if (SUCCEEDED(result) && !text.empty())
        Debug(L"windowAumid.result=SUCCESS value=\"" + Escape(text) + L"\"");
    else if (result == S_OK)
        Debug(L"windowAumid.result=NOT_PRESENT");
    else
        Debug(L"windowAumid.result=API_FAILED hr=" + std::to_wstring(result));
    return text;
}

void LogAumidParts(const wchar_t* name, const std::wstring& aumid)
{
    if (aumid.empty()) return;
    UINT32 familyLength{};
    UINT32 relativeLength{};
    LONG result = ParseApplicationUserModelId(aumid.c_str(), &familyLength, nullptr,
        &relativeLength, nullptr);
    if (result == ERROR_INSUFFICIENT_BUFFER)
    {
        std::wstring family(familyLength, L'\0');
        std::wstring relative(relativeLength, L'\0');
        result = ParseApplicationUserModelId(aumid.c_str(), &familyLength, family.data(),
            &relativeLength, relative.data());
        if (result == ERROR_SUCCESS)
        {
            if (!family.empty() && family.back() == L'\0') family.pop_back();
            if (!relative.empty() && relative.back() == L'\0') relative.pop_back();
            Debug(std::wstring(name) + L".parts.result=SUCCESS packageFamilyName=\"" +
                Escape(family) + L"\" packageRelativeApplicationId=\"" +
                Escape(relative) + L"\"");
        }
    }
    if (result != ERROR_SUCCESS)
        Debug(std::wstring(name) + L".parts.result=" + ResultName(result) +
            L" error=" + ErrorCode(result));
}

const wchar_t* PackagePathTypeName(PackagePathType type)
{
    switch (type)
    {
    case PackagePathType_Install: return L"install";
    case PackagePathType_Effective: return L"effective";
    case PackagePathType_Mutable: return L"mutable";
    case PackagePathType_MachineExternal: return L"machineExternal";
    case PackagePathType_UserExternal: return L"userExternal";
    case PackagePathType_EffectiveExternal: return L"effectiveExternal";
    default: return L"unknown";
    }
}

void LogPackagePath(const wchar_t* name, const WindowsPackagePathProbe& probe)
{
    Debug(std::wstring(L"packagePath.") + name + L".result=" + ResultName(probe.result) +
        L" error=" + ErrorCode(probe.result) + L" value=\"" + Escape(probe.path) + L"\"");
}

void LogProbePackage(const WindowsGameIdentityProbeResult& result)
{
    Debug(L"packageFullName.cacheKey=\"" + Escape(result.packageFullName) + L"\"");
    const auto& metadata = result.package;
    LogPackagePath(PackagePathTypeName(PackagePathType_Install), metadata.installPath);
    LogPackagePath(PackagePathTypeName(PackagePathType_Effective), metadata.effectivePath);
    LogPackagePath(PackagePathTypeName(PackagePathType_Mutable), metadata.mutablePath);
    LogPackagePath(PackagePathTypeName(PackagePathType_MachineExternal), metadata.machineExternalPath);
    LogPackagePath(PackagePathTypeName(PackagePathType_UserExternal), metadata.userExternalPath);
    LogPackagePath(PackagePathTypeName(PackagePathType_EffectiveExternal), metadata.effectiveExternalPath);
    Debug(L"packageInfo.open.result=" + ResultName(metadata.packageInfoResult) +
        L" error=" + ErrorCode(metadata.packageInfoResult));
    if (metadata.packageInfo2Result == ERROR_SUCCESS)
        Debug(L"packageInfo.result=SUCCESS flags=" + std::to_wstring(metadata.packageInfoFlags) +
            L" path=\"" + Escape(metadata.packageInfoPath) + L"\" packageFullName=\"" +
            Escape(metadata.packageInfoFullName) + L"\" packageFamilyName=\"" +
            Escape(metadata.packageInfoFamilyName) + L"\" architecture=" +
            std::to_wstring(metadata.packageInfoArchitecture) + L" version=" +
            std::to_wstring(metadata.packageInfoVersion.Major) + L"." +
            std::to_wstring(metadata.packageInfoVersion.Minor) + L"." +
            std::to_wstring(metadata.packageInfoVersion.Build) + L"." +
            std::to_wstring(metadata.packageInfoVersion.Revision) + L" name=\"" +
            Escape(metadata.packageInfoName) + L"\" publisher=\"" +
            Escape(metadata.packageInfoPublisher) + L"\" publisherId=\"" +
            Escape(metadata.packageInfoPublisherId) + L"\" resourceId=\"" +
            Escape(metadata.packageInfoResourceId) + L"\"");
    else
        Debug(L"packageInfo.result=" + ResultName(metadata.packageInfo2Result) +
            L" error=" + ErrorCode(metadata.packageInfo2Result));
    for (const auto& id : metadata.packageApplicationIds)
        Debug(L"packageApplicationId value=\"" + Escape(id) + L"\"");
    Debug(L"packageApplicationIds.result=" + ResultName(metadata.packageApplicationIdsResult) +
        L" error=" + ErrorCode(metadata.packageApplicationIdsResult) + L" count=" +
        std::to_wstring(metadata.packageApplicationIdCount));
    Debug(L"packageOrigin.result=" + ResultName(metadata.packageOriginResult) +
        L" error=" + ErrorCode(metadata.packageOriginResult) + L" value=" +
        std::to_wstring(metadata.packageOrigin));
    if (result.microsoftGameConfigs.empty())
    {
        Debug(L"microsoftGameConfig.probe.result=NOT_ATTEMPTED reason=no-successful-package-path");
        return;
    }
    for (const auto& location : result.microsoftGameConfigs)
    {
        const auto type = static_cast<PackagePathType>(location.pathType);
        const auto probeResult = location.probeError != 0 ? L"API_FAILED"
            : (location.exists ? L"SUCCESS" : L"NOT_PRESENT");
        const auto readResult = !location.readAttempted ? L"NOT_ATTEMPTED"
            : (location.readable ? L"SUCCESS" : L"API_FAILED");
        Debug(L"microsoftGameConfig.pathType=" + std::wstring(PackagePathTypeName(type)) +
            L" root=\"" + Escape(location.rootPath) + L"\" path=\"" +
            Escape(location.configPath) + L"\" probe.result=" + probeResult +
            L" probe.error=" + std::to_wstring(location.probeError) + L" exists=" +
            std::to_wstring(location.exists ? 1 : 0) + L" read.result=" + readResult +
            L" read.error=" + std::to_wstring(location.readError));
        if (!location.readable) continue;
        Debug(L"storeId=\"" + Escape(location.config.storeId) + L"\" titleId=\"" +
            Escape(location.config.titleId) + L"\" msaAppId=\"" +
            Escape(location.config.msaAppId) + L"\" executableCount=" +
            std::to_wstring(location.config.executables.size()));
        for (const auto& executable : location.config.executables)
            Debug(L"configExecutable name=\"" + Escape(executable.name) + L"\" id=\"" +
                Escape(executable.id) + L"\" targetDeviceFamily=\"" +
                Escape(executable.targetDeviceFamily) + L"\" architecture=\"" +
                Escape(executable.architecture) + L"\"");
        if (!location.executableMatchEvaluated)
            Debug(L"currentExecutableMatch.result=NOT_PRESENT reason=image-name-unavailable");
        else
            Debug(L"currentExecutableMatch.result=SUCCESS value=" +
                std::to_wstring(location.currentExecutableMatched ? 1 : 0));
    }
}
}

WindowsGameIdentitySource::WindowsGameIdentitySource()
    : worker_([this](std::stop_token stop) { WorkerMain(stop); })
{
}

WindowsGameIdentitySource::~WindowsGameIdentitySource()
{
    worker_.request_stop();
    queueWake_.notify_one();
}

void WindowsGameIdentitySource::QueueInspect(HWND foregroundWindow, DWORD processId) noexcept
{
    const Request request{nextSequence_.fetch_add(1, std::memory_order_relaxed),
        GetTickCount64(), foregroundWindow, processId};
    try
    {
        {
            std::lock_guard lock(queueMutex_);
            pendingRequests_.push_back(request);
        }
        queueWake_.notify_one();
    }
    catch (...)
    {
        OutputDebugStringW(L"[GameIdentity] queue.result=API_FAILED reason=allocation\n");
    }
}

void WindowsGameIdentitySource::WorkerMain(std::stop_token stop) noexcept
{
    try
    {
        while (!stop.stop_requested())
        {
            Request request;
            {
                std::unique_lock lock(queueMutex_);
                queueWake_.wait(lock, stop, [this] { return !pendingRequests_.empty(); });
                if (stop.stop_requested()) return;
                request = pendingRequests_.front();
                pendingRequests_.pop_front();
            }
            try
            {
                InspectImpl(request.window, request.processId,
                    request.sequence, request.eventTickMs);
            }
            catch (...)
            {
                try
                {
                    Debug(L"inspection.result=API_FAILED reason=unexpected-exception seq=" +
                        std::to_wstring(request.sequence));
                }
                catch (...)
                {
                    OutputDebugStringW(L"[GameIdentity] inspection.result=API_FAILED "
                        L"reason=unexpected-exception\n");
                }
            }
        }
    }
    catch (...)
    {
        OutputDebugStringW(L"[GameIdentity] worker.result=API_FAILED "
            L"reason=unexpected-exception\n");
    }
}

void WindowsGameIdentitySource::Inspect(HWND foregroundWindow, DWORD processId) noexcept
{
    try
    {
        InspectImpl(foregroundWindow, processId, 0, GetTickCount64());
    }
    catch (...)
    {
        Debug(L"inspection.result=API_FAILED reason=unexpected-exception");
    }
}

void WindowsGameIdentitySource::InspectImpl(HWND foregroundWindow, DWORD processId,
    std::uint64_t sequence, ULONGLONG eventTickMs)
{
    if (foregroundWindow == lastWindow_ && processId == lastProcessId_)
        return;
    lastWindow_ = foregroundWindow;
    lastProcessId_ = processId;
    std::wstringstream hwnd;
    hwnd << L"0x" << std::hex << std::uppercase
        << reinterpret_cast<ULONG_PTR>(foregroundWindow);
    const ULONGLONG processingTickMs = GetTickCount64();
    Debug(L"trigger=foreground-change seq=" + std::to_wstring(sequence) +
        L" eventTickMs=" + std::to_wstring(eventTickMs) +
        L" processingTickMs=" + std::to_wstring(processingTickMs) +
        L" processingDelayMs=" + std::to_wstring(
            processingTickMs >= eventTickMs ? processingTickMs - eventTickMs : 0) +
        L" hwnd=" + hwnd.str() + L" pid=" + std::to_wstring(processId));
    if (foregroundWindow)
    {
        std::array<wchar_t, 1024> title{};
        GetWindowTextW(foregroundWindow, title.data(), static_cast<int>(title.size()));
        Debug(L"pid=" + std::to_wstring(processId) + L" windowVisible=" +
            std::to_wstring(IsWindowVisible(foregroundWindow) ? 1 : 0) +
            L" title=\"" + Escape(title.data()) + L"\"");
        const auto windowAumid = ReadWindowAumid(foregroundWindow);
        LogAumidParts(L"windowAumid", windowAumid);
    }
    if (!processId)
    {
        Debug(L"process.result=NOT_PRESENT reason=no-foreground-pid error=0");
        return;
    }

    const auto result = probe_.Inspect(processId);
    if (!result.processOpened)
    {
        const wchar_t* state = result.processOpenError == ERROR_INVALID_PARAMETER
            ? L"PROCESS_EXITED"
            : (result.processOpenError == ERROR_ACCESS_DENIED ? L"ACCESS_DENIED" : L"API_FAILED");
        Debug(L"process.result=" + std::wstring(state) + L" error=" +
            ErrorCode(result.processOpenError));
        return;
    }
    if (!result.imagePath.empty())
        Debug(L"pid=" + std::to_wstring(processId) + L" image=\"" +
            Escape(result.imagePath) + L"\"");
    else
        Debug(L"process.image.result=API_FAILED error=" + ErrorCode(result.imagePathError));
    Debug(L"pid=" + std::to_wstring(processId) + L" executable=\"" +
        Escape(result.executableName) + L"\"");
    LogAumidParts(L"processAumid", result.processAumid);
    Debug(L"processAumid.result=" + ResultName(result.processAumidResult) +
        L" error=" + ErrorCode(result.processAumidResult) + L" value=\"" +
        Escape(result.processAumid) + L"\"");
    Debug(L"packageFullName.result=" + ResultName(result.packageFullNameResult) +
        L" error=" + ErrorCode(result.packageFullNameResult) + L" value=\"" +
        Escape(result.packageFullName) + L"\"");
    Debug(L"packageFamilyName.result=" + ResultName(result.packageFamilyNameResult) +
        L" error=" + ErrorCode(result.packageFamilyNameResult) + L" value=\"" +
        Escape(result.packageFamilyName) + L"\"");
    if (result.packageIdentityResult == ERROR_SUCCESS)
        Debug(L"packageIdentity.result=SUCCESS version=" +
            std::to_wstring(result.packageIdentityVersion.Major) + L"." +
            std::to_wstring(result.packageIdentityVersion.Minor) + L"." +
            std::to_wstring(result.packageIdentityVersion.Build) + L"." +
            std::to_wstring(result.packageIdentityVersion.Revision) +
            L" architecture=" + std::to_wstring(result.packageIdentityArchitecture) +
            L" name=\"" + Escape(result.packageIdentityName) + L"\" publisher=\"" +
            Escape(result.packageIdentityPublisher) + L"\" publisherId=\"" +
            Escape(result.packageIdentityPublisherId) + L"\" resourceId=\"" +
            Escape(result.packageIdentityResourceId) + L"\"");
    else
        Debug(L"packageIdentity.result=" + ResultName(result.packageIdentityResult) +
            L" error=" + ErrorCode(result.packageIdentityResult));
    if (result.packageMetadataAvailable)
        LogProbePackage(result);
}
}
