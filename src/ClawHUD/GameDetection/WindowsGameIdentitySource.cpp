#include "WindowsGameIdentitySource.h"

#include "RuntimeLogger.h"

#include <appmodel.h>
#include <shtypes.h>
#include <propkey.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#pragma comment(lib, "propsys.lib")

namespace clawhud
{
namespace
{
std::wstring ErrorCode(DWORD error)
{
    return std::to_wstring(error);
}

std::wstring ResultName(LONG result)
{
    if (result == ERROR_SUCCESS) return L"SUCCESS";
    if (result == APPMODEL_ERROR_NO_PACKAGE) return L"NO_PACKAGE";
    if (result == APPMODEL_ERROR_NO_APPLICATION) return L"NOT_PRESENT";
    if (result == ERROR_PROC_NOT_FOUND) return L"SYMBOL_MISSING";
    if (result == ERROR_ACCESS_DENIED) return L"ACCESS_DENIED";
    if (result == ERROR_INSUFFICIENT_BUFFER) return L"BUFFER_REQUIRED";
    return L"API_FAILED";
}

void Debug(const std::wstring& message)
{
    RuntimeLogger::Log(RuntimeLogLevel::Debug, L"[GameIdentity] " + message);
}

std::wstring Escape(std::wstring value)
{
    std::wstring result;
    for (const wchar_t character : value)
    {
        if (character == L'\r') result += L"\\r";
        else if (character == L'\n') result += L"\\n";
        else if (character == L'\t') result += L"\\t";
        else result += character;
    }
    return result;
}

std::wstring ReadElement(std::wstring_view xml, const wchar_t* name)
{
    const std::wstring input(xml);
    const std::wstring expression = L"<\\s*(?:[A-Za-z0-9_.-]+:)?" +
        std::wstring(name) + L"\\s*>\\s*([^<]*)<\\s*/\\s*(?:[A-Za-z0-9_.-]+:)?" +
        std::wstring(name) + L"\\s*>";
    std::wregex pattern(expression, std::regex_constants::icase);
    std::match_results<std::wstring::const_iterator> match;
    if (std::regex_search(input.begin(), input.end(), match, pattern))
        return std::wstring(match[1].first, match[1].second);
    return {};
}

std::wstring ReadAttribute(std::wstring_view tag, const wchar_t* name)
{
    const std::wstring input(tag);
    const std::wstring expression = std::wstring(name) +
        L"\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')";
    std::wregex pattern(expression, std::regex_constants::icase);
    std::match_results<std::wstring::const_iterator> match;
    if (!std::regex_search(input.begin(), input.end(), match, pattern))
        return {};
    if (match[1].matched) return std::wstring(match[1].first, match[1].second);
    if (match[2].matched) return std::wstring(match[2].first, match[2].second);
    return {};
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!length) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring QueryProcessString(HANDLE process,
    LONG (*query)(HANDLE, UINT32*, PWSTR), const wchar_t* name,
    LONG& resultCode)
{
    UINT32 length{};
    resultCode = query(process, &length, nullptr);
    if (resultCode != ERROR_INSUFFICIENT_BUFFER)
    {
        Debug(std::wstring(name) + L".result=" + ResultName(resultCode) +
            L" error=" + ErrorCode(resultCode));
        return {};
    }
    std::wstring value(length, L'\0');
    resultCode = query(process, &length, value.data());
    if (resultCode != ERROR_SUCCESS)
    {
        Debug(std::wstring(name) + L".result=" + ResultName(resultCode) +
            L" error=" + ErrorCode(resultCode));
        return {};
    }
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    Debug(std::wstring(name) + L".result=SUCCESS value=\"" + Escape(value) + L"\"");
    return value;
}

std::wstring ReadWindowAumid(HWND window)
{
    IPropertyStore* store{};
    const HRESULT storeResult = SHGetPropertyStoreForWindow(window,
        IID_PPV_ARGS(&store));
    if (FAILED(storeResult))
    {
        Debug(L"windowAumid.result=API_FAILED hr=" + std::to_wstring(storeResult));
        return {};
    }
    PROPVARIANT value{};
    PropVariantInit(&value);
    const HRESULT result = store->GetValue(PKEY_AppUserModel_ID, &value);
    std::wstring text;
    if (SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal)
        text = value.pwszVal;
    if (SUCCEEDED(result) && !text.empty())
        Debug(L"windowAumid.result=SUCCESS value=\"" + Escape(text) + L"\"");
    else if (result == S_OK)
        Debug(L"windowAumid.result=NOT_PRESENT");
    else
        Debug(L"windowAumid.result=API_FAILED hr=" + std::to_wstring(result));
    PropVariantClear(&value);
    store->Release();
    return text;
}

using GetPackagePathByFullName2Fn = LONG (WINAPI*)(PCWSTR, PackagePathType, UINT32*, PWSTR);
using GetPackageInfo2Fn = LONG (WINAPI*)(PACKAGE_INFO_REFERENCE, UINT32, PackagePathType,
    UINT32*, BYTE*, UINT32*);

template <typename Function>
Function ResolveKernelApi(const char* name)
{
    HMODULE module = GetModuleHandleW(L"kernelbase.dll");
    if (!module) module = GetModuleHandleW(L"kernel32.dll");
    return module ? reinterpret_cast<Function>(GetProcAddress(module, name)) : nullptr;
}

WindowsGameIdentitySource::PackageStaticMetadata::PackagePathProbe QueryPackagePath(
    const std::wstring& fullName, PackagePathType pathType)
{
    WindowsGameIdentitySource::PackageStaticMetadata::PackagePathProbe probe;
    const auto query = ResolveKernelApi<GetPackagePathByFullName2Fn>(
        "GetPackagePathByFullName2");
    if (!query)
    {
        probe.result = ERROR_PROC_NOT_FOUND;
        return probe;
    }
    UINT32 length{};
    probe.result = query(fullName.c_str(), pathType, &length, nullptr);
    if (probe.result != ERROR_INSUFFICIENT_BUFFER) return probe;
    std::wstring path(length, L'\0');
    probe.result = query(fullName.c_str(), pathType, &length, path.data());
    if (probe.result != ERROR_SUCCESS) return probe;
    if (!path.empty() && path.back() == L'\0') path.pop_back();
    probe.path = std::move(path);
    return probe;
}

const wchar_t* PackagePathTypeName(PackagePathType type)
{
    switch (type)
    {
    case PackagePathType_Install: return L"install";
    case PackagePathType_Effective: return L"effective";
    case PackagePathType_Mutable: return L"mutable";
    default: return L"unknown";
    }
}

void LogPackagePath(const wchar_t* name,
    const WindowsGameIdentitySource::PackageStaticMetadata::PackagePathProbe& probe)
{
    Debug(std::wstring(L"packagePath.") + name + L".result=" + ResultName(probe.result) +
        L" error=" + ErrorCode(probe.result) + L" value=\"" + Escape(probe.path) + L"\"");
}

void QueryPackageInfo(const std::wstring& fullName,
    WindowsGameIdentitySource::PackageStaticMetadata& metadata)
{
    PACKAGE_INFO_REFERENCE reference{};
    metadata.packageInfoResult = OpenPackageInfoByFullName(fullName.c_str(), 0, &reference);
    Debug(L"packageInfo.open.result=" + ResultName(metadata.packageInfoResult) +
        L" error=" + ErrorCode(metadata.packageInfoResult));
    if (metadata.packageInfoResult != ERROR_SUCCESS) return;

    const auto query = ResolveKernelApi<GetPackageInfo2Fn>("GetPackageInfo2");
    if (!query)
    {
        metadata.packageInfo2Result = ERROR_PROC_NOT_FOUND;
        Debug(L"packageInfo.result=SYMBOL_MISSING error=" +
            ErrorCode(ERROR_PROC_NOT_FOUND));
        ClosePackageInfo(reference);
        return;
    }
    UINT32 length{};
    UINT32 count{};
    LONG result = query(reference, 0, PackagePathType_Effective,
        &length, nullptr, &count);
    metadata.packageInfo2Result = result;
    if (result == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<BYTE> buffer(length);
        result = query(reference, 0, PackagePathType_Effective,
            &length, buffer.data(), &count);
        metadata.packageInfo2Result = result;
        if (result == ERROR_SUCCESS && count != 0)
        {
            const auto* info = reinterpret_cast<const PACKAGE_INFO*>(buffer.data());
            metadata.packageInfoFlags = info->flags;
            Debug(L"packageInfo.result=SUCCESS count=" + std::to_wstring(count) +
                L" flags=" + std::to_wstring(info->flags) + L" path=\"" +
                Escape(info->path ? info->path : L"") + L"\" packageFullName=\"" +
                Escape(info->packageFullName ? info->packageFullName : L"") +
                L"\" packageFamilyName=\"" +
                Escape(info->packageFamilyName ? info->packageFamilyName : L"") +
                L"\" architecture=" + std::to_wstring(info->packageId.processorArchitecture) +
                L" version=" + std::to_wstring(info->packageId.version.Major) + L"." +
                std::to_wstring(info->packageId.version.Minor) + L"." +
                std::to_wstring(info->packageId.version.Build) + L"." +
                std::to_wstring(info->packageId.version.Revision) +
                L" publisher=\"" + Escape(info->packageId.publisher ? info->packageId.publisher : L"") +
                L"\" publisherId=\"" +
                Escape(info->packageId.publisherId ? info->packageId.publisherId : L"") +
                L"\" resourceId=\"" +
                Escape(info->packageId.resourceId ? info->packageId.resourceId : L"") + L"\"");
        }
    }
    if (result != ERROR_SUCCESS)
        Debug(L"packageInfo.result=" + ResultName(result) + L" error=" + ErrorCode(result));
    ClosePackageInfo(reference);
}

std::wstring ReadFileText(const std::filesystem::path& path, bool& readable)
{
    readable = false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe)
    {
        readable = true;
        const auto* characters = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
        return std::wstring(characters, (bytes.size() - 2) / sizeof(wchar_t));
    }
    auto text = Utf8ToWide(bytes);
    if (text.empty() && !bytes.empty()) return {};
    readable = true;
    return text;
}
}

MicrosoftGameConfigSnapshot ParseMicrosoftGameConfig(std::wstring_view xml)
{
    const std::wstring input(xml);
    MicrosoftGameConfigSnapshot result;
    if (xml.empty() || xml.find(L'<') == std::wstring_view::npos)
        return result;
    result.storeId = ReadElement(xml, L"StoreId");
    result.titleId = ReadElement(xml, L"TitleId");
    result.msaAppId = ReadElement(xml, L"MsaAppId");
    if (result.msaAppId.empty()) result.msaAppId = ReadElement(xml, L"MSAAppId");
    std::wregex executablePattern(L"<\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\b[^>]*>([^<]*)<\\s*/\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\s*>",
        std::regex_constants::icase);
    for (std::wsregex_iterator iterator(input.begin(), input.end(), executablePattern), end;
        iterator != end; ++iterator)
    {
        const auto tagStart = input.begin() + iterator->position();
        const auto tagEnd = std::find(tagStart, input.end(), L'>');
        std::wstring_view tag(tagStart, tagEnd);
        result.executables.push_back({
            ReadAttribute(tag, L"Name").empty()
                ? std::wstring((*iterator)[1].first, (*iterator)[1].second)
                : ReadAttribute(tag, L"Name"),
            ReadAttribute(tag, L"Id"),
            ReadAttribute(tag, L"TargetDeviceFamily"),
            ReadAttribute(tag, L"Architecture")});
    }
    std::wregex selfClosingExecutablePattern(
        L"<\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\b[^>]*/\\s*>",
        std::regex_constants::icase);
    for (std::wsregex_iterator iterator(input.begin(), input.end(),
        selfClosingExecutablePattern), end; iterator != end; ++iterator)
    {
        const std::wstring tag = iterator->str();
        result.executables.push_back({ReadAttribute(tag, L"Name"),
            ReadAttribute(tag, L"Id"), ReadAttribute(tag, L"TargetDeviceFamily"),
            ReadAttribute(tag, L"Architecture")});
    }
    std::wregex rootPattern(L"<\\s*(?:[A-Za-z0-9_.-]+:)?(?:MicrosoftGame|Game)\\b",
        std::regex_constants::icase);
    result.recognizedGameRoot = std::regex_search(input, rootPattern);
    return result;
}

bool WindowsExecutableNamesMatch(std::wstring_view left, std::wstring_view right) noexcept
{
    const auto base = [](std::wstring_view value)
    {
        const auto slash = value.find_last_of(L"\\/");
        return value.substr(slash == std::wstring_view::npos ? 0 : slash + 1);
    };
    const auto leftBase = base(left);
    const auto rightBase = base(right);
    return leftBase.size() == rightBase.size() &&
        std::equal(leftBase.begin(), leftBase.end(), rightBase.begin(),
            [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

std::wstring PackageMetadataCacheKey(std::wstring_view packageFullName)
{
    return std::wstring(packageFullName);
}

void WindowsGameIdentitySource::Inspect(HWND foregroundWindow, DWORD processId) noexcept
{
    try
    {
        InspectImpl(foregroundWindow, processId);
    }
    catch (...)
    {
        Debug(L"inspection.result=API_FAILED reason=unexpected-exception");
    }
}

void WindowsGameIdentitySource::InspectImpl(HWND foregroundWindow, DWORD processId)
{
    if (foregroundWindow == lastWindow_ && processId == lastProcessId_)
        return;
    lastWindow_ = foregroundWindow;
    lastProcessId_ = processId;
    Debug(L"trigger=foreground-change hwnd=0x" +
        std::to_wstring(reinterpret_cast<ULONG_PTR>(foregroundWindow)) +
        L" pid=" + std::to_wstring(processId));
    if (!processId)
    {
        Debug(L"process.result=PROCESS_EXITED error=0");
        return;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
    {
        Debug(L"process.result=API_FAILED error=" + ErrorCode(GetLastError()));
        return;
    }
    wchar_t imagePath[32768]{};
    DWORD imageLength = static_cast<DWORD>(std::size(imagePath));
    if (QueryFullProcessImageNameW(process, 0, imagePath, &imageLength))
    {
        const std::wstring image(imagePath, imageLength);
        const auto executable = std::filesystem::path(image).filename().wstring();
        Debug(L"pid=" + std::to_wstring(processId) + L" image=\"" + Escape(image) + L"\"");
        Debug(L"pid=" + std::to_wstring(processId) + L" executable=\"" + Escape(executable) + L"\"");
        if (foregroundWindow)
        {
            wchar_t title[1024]{};
            GetWindowTextW(foregroundWindow, title, static_cast<int>(std::size(title)));
            Debug(L"pid=" + std::to_wstring(processId) + L" windowVisible=" +
                std::to_wstring(IsWindowVisible(foregroundWindow) ? 1 : 0) +
                L" title=\"" + Escape(title) + L"\"");
            ReadWindowAumid(foregroundWindow);
        }
        LONG resultCode{};
        const auto processAumid = QueryProcessString(process, GetApplicationUserModelId,
            L"processAumid", resultCode);
        const auto packageFullName = QueryProcessString(process, GetPackageFullName,
            L"packageFullName", resultCode);
        QueryProcessString(process, GetPackageFamilyName, L"packageFamilyName", resultCode);
        UINT32 packageIdLength{};
        LONG packageIdResult = GetPackageId(process, &packageIdLength, nullptr);
        if (packageIdResult == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<BYTE> packageIdBuffer(packageIdLength);
            packageIdResult = GetPackageId(process, &packageIdLength,
                packageIdBuffer.data());
            if (packageIdResult == ERROR_SUCCESS)
            {
                const auto* packageId = reinterpret_cast<const PACKAGE_ID*>(
                    packageIdBuffer.data());
                Debug(L"packageIdentity.result=SUCCESS version=" +
                    std::to_wstring(packageId->version.Major) + L"." +
                    std::to_wstring(packageId->version.Minor) + L"." +
                    std::to_wstring(packageId->version.Build) + L"." +
                    std::to_wstring(packageId->version.Revision) +
                    L" architecture=" + std::to_wstring(packageId->processorArchitecture) +
                    L" publisher=\"" + Escape(packageId->publisher ? packageId->publisher : L"") +
                    L"\" publisherId=\"" + Escape(packageId->publisherId ? packageId->publisherId : L"") +
                    L"\" resourceId=\"" + Escape(packageId->resourceId ? packageId->resourceId : L"") +
                    L"\"");
            }
        }
        if (packageIdResult != ERROR_SUCCESS)
            Debug(L"packageIdentity.result=" + ResultName(packageIdResult) +
                L" error=" + ErrorCode(packageIdResult));
        if (!packageFullName.empty())
            InspectPackage(packageFullName, executable);
        (void)processAumid;
    }
    else
        Debug(L"process.image.result=API_FAILED error=" + ErrorCode(GetLastError()));
    CloseHandle(process);
}

void WindowsGameIdentitySource::InspectPackage(const std::wstring& packageFullName,
    const std::wstring& executableName)
{
    Debug(L"packageFullName.cacheKey=\"" + Escape(packageFullName) + L"\"");
    const auto cacheKey = PackageMetadataCacheKey(packageFullName);
    auto found = packageCache_.find(cacheKey);
    if (found == packageCache_.end())
    {
        PackageStaticMetadata metadata;
        metadata.installPath = QueryPackagePath(packageFullName, PackagePathType_Install);
        metadata.effectivePath = QueryPackagePath(packageFullName, PackagePathType_Effective);
        metadata.mutablePath = QueryPackagePath(packageFullName, PackagePathType_Mutable);
        QueryPackageInfo(packageFullName, metadata);
        const auto path = !metadata.effectivePath.path.empty()
            ? metadata.effectivePath.path
            : (!metadata.installPath.path.empty() ? metadata.installPath.path
                : metadata.mutablePath.path);
        if (!path.empty())
        {
            metadata.configPath = (std::filesystem::path(path) / L"MicrosoftGame.config").wstring();
            std::error_code error;
            metadata.configExists = std::filesystem::exists(metadata.configPath, error);
            metadata.configProbeError = error.value();
        }
    if (metadata.configExists)
        {
            bool readable{};
            const auto text = ReadFileText(metadata.configPath, readable);
            metadata.configReadable = readable;
            if (readable) metadata.config = ParseMicrosoftGameConfig(text);
        }
        found = packageCache_.emplace(cacheKey, std::move(metadata)).first;
    }
    const auto& metadata = found->second;
    LogPackagePath(PackagePathTypeName(PackagePathType_Install), metadata.installPath);
    LogPackagePath(PackagePathTypeName(PackagePathType_Effective), metadata.effectivePath);
    LogPackagePath(PackagePathTypeName(PackagePathType_Mutable), metadata.mutablePath);
    const auto configProbeResult = metadata.configPath.empty()
        ? (metadata.effectivePath.path.empty() && metadata.installPath.path.empty() &&
            metadata.mutablePath.path.empty() ? L"NO_PACKAGE" : L"NOT_PRESENT")
        : (metadata.configProbeError != 0 ? L"API_FAILED"
            : (metadata.configExists ? L"SUCCESS" : L"NOT_PRESENT"));
    Debug(L"microsoftGameConfig.path=\"" + Escape(metadata.configPath) +
        L"\" probe.result=" + configProbeResult + L" error=" +
        std::to_wstring(metadata.configProbeError));
    Debug(L"microsoftGameConfig.exists=" + std::to_wstring(metadata.configExists ? 1 : 0) +
        L" readable=" + std::to_wstring(metadata.configReadable ? 1 : 0));
    if (!metadata.configReadable) return;
    Debug(L"storeId=\"" + Escape(metadata.config.storeId) + L"\" titleId=\"" +
        Escape(metadata.config.titleId) + L"\" msaAppId=\"" +
        Escape(metadata.config.msaAppId) + L"\"");
    Debug(L"configExecutableCount=" + std::to_wstring(metadata.config.executables.size()));
    bool match = false;
    for (const auto& executable : metadata.config.executables)
    {
        Debug(L"configExecutable name=\"" + Escape(executable.name) + L"\" id=\"" +
            Escape(executable.id) + L"\" targetDeviceFamily=\"" +
            Escape(executable.targetDeviceFamily) + L"\" architecture=\"" +
            Escape(executable.architecture) + L"\"");
        match = match || WindowsExecutableNamesMatch(executableName, executable.name);
    }
    Debug(L"currentExecutableMatch=" + std::to_wstring(match ? 1 : 0));
}
}
