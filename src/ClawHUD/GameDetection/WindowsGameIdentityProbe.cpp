#include "WindowsGameIdentityProbe.h"

#include <appmodel.h>

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <regex>
#include <utility>

namespace clawhud
{
namespace
{
struct UniqueHandle
{
    HANDLE value{};
    ~UniqueHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    HANDLE get() const noexcept { return value; }
};

struct UniquePackageInfo
{
    PACKAGE_INFO_REFERENCE value{};
    ~UniquePackageInfo() { if (value) ClosePackageInfo(value); }
};

std::wstring RemoveXmlComments(std::wstring_view xml)
{
    std::wstring result;
    std::size_t cursor = 0;
    while (cursor < xml.size())
    {
        const auto begin = xml.find(L"<!--", cursor);
        if (begin == std::wstring_view::npos)
        {
            result.append(xml.substr(cursor));
            break;
        }
        result.append(xml.substr(cursor, begin - cursor));
        const auto end = xml.find(L"-->", begin + 4);
        if (end == std::wstring_view::npos) break;
        cursor = end + 3;
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

struct FileReadProbe
{
    bool readable{};
    DWORD error{ERROR_SUCCESS};
    std::wstring text;
};

FileReadProbe ReadFileText(const std::filesystem::path& path)
{
    FileReadProbe probe;
    UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file.get())
    {
        probe.error = GetLastError();
        return probe;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size))
    {
        probe.error = GetLastError();
        return probe;
    }
    if (size.QuadPart < 0 ||
        static_cast<ULONGLONG>(size.QuadPart) > std::numeric_limits<DWORD>::max())
    {
        probe.error = ERROR_FILE_TOO_LARGE;
        return probe;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD bytesRead{};
    if (!bytes.empty() && !ReadFile(file.get(), bytes.data(),
        static_cast<DWORD>(bytes.size()), &bytesRead, nullptr))
    {
        probe.error = GetLastError();
        return probe;
    }
    bytes.resize(bytesRead);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe)
    {
        if ((bytes.size() - 2) % sizeof(wchar_t) != 0)
        {
            probe.error = ERROR_INVALID_DATA;
            return probe;
        }
        probe.text.resize((bytes.size() - 2) / sizeof(wchar_t));
        std::memcpy(probe.text.data(), bytes.data() + 2, bytes.size() - 2);
        probe.readable = true;
        return probe;
    }
    probe.text = Utf8ToWide(bytes);
    if (probe.text.empty() && !bytes.empty())
    {
        probe.error = ERROR_NO_UNICODE_TRANSLATION;
        return probe;
    }
    probe.readable = true;
    return probe;
}

using GetPackagePathByFullName2Fn = LONG (WINAPI*)(PCWSTR, PackagePathType, UINT32*, PWSTR);
using GetPackageInfo2Fn = LONG (WINAPI*)(PACKAGE_INFO_REFERENCE, UINT32, PackagePathType,
    UINT32*, BYTE*, UINT32*);
using GetStagedPackageOriginFn = LONG (WINAPI*)(PCWSTR, PackageOrigin*);

template <typename Function>
Function ResolveKernelApi(const char* name)
{
    for (const wchar_t* moduleName : {L"kernelbase.dll", L"kernel32.dll"})
    {
        if (HMODULE module = GetModuleHandleW(moduleName))
            if (auto proc = GetProcAddress(module, name))
                return reinterpret_cast<Function>(proc);
    }
    return nullptr;
}

WindowsPackagePathProbe QueryPackagePath(const std::wstring& fullName,
    PackagePathType pathType)
{
    WindowsPackagePathProbe probe;
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

void QueryPackageInfo(const std::wstring& fullName, WindowsPackageStaticMetadata& metadata)
{
    constexpr UINT32 kPackageInfoFlags = PACKAGE_INFORMATION_FULL | PACKAGE_FILTER_HEAD;
    UniquePackageInfo reference;
    metadata.packageInfoResult = OpenPackageInfoByFullName(fullName.c_str(), 0,
        &reference.value);
    if (metadata.packageInfoResult != ERROR_SUCCESS) return;

    const auto originQuery = ResolveKernelApi<GetStagedPackageOriginFn>(
        "GetStagedPackageOrigin");
    if (!originQuery)
        metadata.packageOriginResult = ERROR_PROC_NOT_FOUND;
    else
    {
        PackageOrigin origin{};
        metadata.packageOriginResult = originQuery(fullName.c_str(), &origin);
        metadata.packageOrigin = static_cast<UINT32>(origin);
    }

    const auto query = ResolveKernelApi<GetPackageInfo2Fn>("GetPackageInfo2");
    if (!query)
        metadata.packageInfo2Result = ERROR_PROC_NOT_FOUND;
    else
    {
        UINT32 length{};
        UINT32 count{};
        LONG result = query(reference.value, kPackageInfoFlags, PackagePathType_Effective,
            &length, nullptr, &count);
        metadata.packageInfo2Result = result;
        if (result == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<BYTE> buffer(length);
            result = query(reference.value, kPackageInfoFlags, PackagePathType_Effective,
                &length, buffer.data(), &count);
            metadata.packageInfo2Result = result;
            metadata.packageInfoCount = count;
            if (result == ERROR_SUCCESS && count == 1)
            {
                const auto* info = reinterpret_cast<const PACKAGE_INFO*>(buffer.data());
                metadata.packageInfoFlags = info->flags;
                metadata.packageInfoPath = info->path ? info->path : L"";
                metadata.packageInfoFullName = info->packageFullName ?
                    info->packageFullName : L"";
                metadata.packageInfoFamilyName = info->packageFamilyName ?
                    info->packageFamilyName : L"";
                metadata.packageInfoName = info->packageId.name ? info->packageId.name : L"";
                metadata.packageInfoPublisher = info->packageId.publisher ?
                    info->packageId.publisher : L"";
                metadata.packageInfoPublisherId = info->packageId.publisherId ?
                    info->packageId.publisherId : L"";
                metadata.packageInfoResourceId = info->packageId.resourceId ?
                    info->packageId.resourceId : L"";
                metadata.packageInfoArchitecture = info->packageId.processorArchitecture;
                metadata.packageInfoVersion = info->packageId.version;
            }
        }
    }

    UINT32 length{};
    UINT32 count{};
    LONG result = GetPackageApplicationIds(reference.value, &length, nullptr, &count);
    if (result == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<BYTE> buffer(length);
        result = GetPackageApplicationIds(reference.value, &length,
            buffer.data(), &count);
        if (result == ERROR_SUCCESS)
        {
            const auto ids = reinterpret_cast<PCWSTR*>(buffer.data());
            for (UINT32 index = 0; index < count; ++index)
                metadata.packageApplicationIds.emplace_back(ids[index] ? ids[index] : L"");
        }
    }
    metadata.packageApplicationIdsResult = result;
    metadata.packageApplicationIdCount = count;
}

void ProbeGameConfigLocation(PackagePathType type,
    const WindowsPackagePathProbe& pathProbe,
    std::vector<MicrosoftGameConfigProbeResult>& output)
{
    if (pathProbe.result != ERROR_SUCCESS || pathProbe.path.empty()) return;
    if (std::any_of(output.begin(), output.end(), [&](const auto& existing)
        { return _wcsicmp(existing.rootPath.c_str(), pathProbe.path.c_str()) == 0; }))
        return;

    MicrosoftGameConfigProbeResult probe;
    probe.pathType = static_cast<int>(type);
    probe.rootPath = pathProbe.path;
    probe.configPath = (std::filesystem::path(pathProbe.path) /
        L"MicrosoftGame.config").wstring();
    probe.probeAttempted = true;
    std::error_code error;
    probe.exists = std::filesystem::exists(probe.configPath, error);
    probe.probeError = error.value();
    if (!error && probe.exists)
    {
        probe.readAttempted = true;
        const auto read = ReadFileText(probe.configPath);
        probe.readable = read.readable;
        probe.readError = read.error;
        if (read.readable) probe.config = ParseMicrosoftGameConfig(read.text);
    }
    output.push_back(std::move(probe));
}

std::wstring QueryProcessString(HANDLE process,
    LONG (*query)(HANDLE, UINT32*, PWSTR), LONG& resultCode)
{
    UINT32 length{};
    resultCode = query(process, &length, nullptr);
    if (resultCode != ERROR_INSUFFICIENT_BUFFER)
        return {};
    std::wstring value(length, L'\0');
    resultCode = query(process, &length, value.data());
    if (resultCode != ERROR_SUCCESS)
        return {};
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

WindowsPackageStaticMetadata BuildPackageMetadata(const std::wstring& fullName)
{
    WindowsPackageStaticMetadata metadata;
    metadata.installPath = QueryPackagePath(fullName, PackagePathType_Install);
    metadata.effectivePath = QueryPackagePath(fullName, PackagePathType_Effective);
    metadata.mutablePath = QueryPackagePath(fullName, PackagePathType_Mutable);
    metadata.machineExternalPath = QueryPackagePath(fullName, PackagePathType_MachineExternal);
    metadata.userExternalPath = QueryPackagePath(fullName, PackagePathType_UserExternal);
    metadata.effectiveExternalPath = QueryPackagePath(fullName, PackagePathType_EffectiveExternal);
    QueryPackageInfo(fullName, metadata);
    ProbeGameConfigLocation(PackagePathType_Install, metadata.installPath, metadata.configLocations);
    ProbeGameConfigLocation(PackagePathType_Effective, metadata.effectivePath, metadata.configLocations);
    ProbeGameConfigLocation(PackagePathType_Mutable, metadata.mutablePath, metadata.configLocations);
    ProbeGameConfigLocation(PackagePathType_MachineExternal, metadata.machineExternalPath, metadata.configLocations);
    ProbeGameConfigLocation(PackagePathType_UserExternal, metadata.userExternalPath, metadata.configLocations);
    ProbeGameConfigLocation(PackagePathType_EffectiveExternal, metadata.effectiveExternalPath, metadata.configLocations);
    return metadata;
}
}

MicrosoftGameConfigSnapshot ParseMicrosoftGameConfig(std::wstring_view xml)
{
    const std::wstring input = RemoveXmlComments(xml);
    MicrosoftGameConfigSnapshot result;
    if (xml.empty() || xml.find(L'<') == std::wstring_view::npos)
        return result;
    result.storeId = ReadElement(input, L"StoreId");
    result.titleId = ReadElement(input, L"TitleId");
    result.msaAppId = ReadElement(input, L"MsaAppId");
    if (result.msaAppId.empty()) result.msaAppId = ReadElement(input, L"MSAAppId");
    std::wregex executablePattern(
        L"<\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\b[^>]*>([^<]*)<\\s*/\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\s*>",
        std::regex_constants::icase);
    for (std::wsregex_iterator iterator(input.begin(), input.end(), executablePattern), end;
        iterator != end; ++iterator)
    {
        const auto tagStart = input.begin() + iterator->position();
        const auto tagEnd = std::find(tagStart, input.end(), L'>');
        const std::wstring_view tag(tagStart, tagEnd);
        result.executables.push_back({
            ReadAttribute(tag, L"Name").empty()
                ? std::wstring((*iterator)[1].first, (*iterator)[1].second)
                : ReadAttribute(tag, L"Name"),
            ReadAttribute(tag, L"Id"), ReadAttribute(tag, L"TargetDeviceFamily"),
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
    if (leftBase.empty() || rightBase.empty()) return false;
    return leftBase.size() == rightBase.size() &&
        std::equal(leftBase.begin(), leftBase.end(), rightBase.begin(),
            [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

std::wstring PackageMetadataCacheKey(std::wstring_view packageFullName)
{
    return std::wstring(packageFullName);
}

bool HasReadableMicrosoftGameExecutableMatch(
    const WindowsGameIdentityProbeResult& result) noexcept
{
    return std::any_of(result.microsoftGameConfigs.begin(),
        result.microsoftGameConfigs.end(), [](const auto& location)
        { return location.readable && location.currentExecutableMatched; });
}

WindowsGameIdentityProbeResult WindowsGameIdentityProbe::Inspect(DWORD processId)
{
    WindowsGameIdentityProbeResult result;
    result.processId = processId;
    if (!processId)
    {
        result.processOpenError = ERROR_INVALID_PARAMETER;
        return result;
    }

    UniqueHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)};
    if (!process.get())
    {
        result.processOpenError = GetLastError();
        return result;
    }
    result.processOpened = true;

    wchar_t imagePath[32768]{};
    DWORD imageLength = static_cast<DWORD>(std::size(imagePath));
    if (QueryFullProcessImageNameW(process.get(), 0, imagePath, &imageLength))
    {
        result.imagePath.assign(imagePath, imageLength);
        result.executableName = std::filesystem::path(result.imagePath).filename().wstring();
    }
    else
    {
        result.imagePathError = GetLastError();
    }

    result.processAumid = QueryProcessString(process.get(), GetApplicationUserModelId,
        result.processAumidResult);
    result.packageFullName = QueryProcessString(process.get(), GetPackageFullName,
        result.packageFullNameResult);
    result.packageFamilyName = QueryProcessString(process.get(), GetPackageFamilyName,
        result.packageFamilyNameResult);

    UINT32 packageIdLength{};
    result.packageIdentityResult = GetPackageId(process.get(), &packageIdLength, nullptr);
    if (result.packageIdentityResult == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<BYTE> packageIdBuffer(packageIdLength);
        result.packageIdentityResult = GetPackageId(process.get(), &packageIdLength,
            packageIdBuffer.data());
        if (result.packageIdentityResult == ERROR_SUCCESS)
        {
            const auto* packageId = reinterpret_cast<const PACKAGE_ID*>(
                packageIdBuffer.data());
            result.packageIdentityArchitecture = packageId->processorArchitecture;
            result.packageIdentityVersion = packageId->version;
            result.packageIdentityName = packageId->name ? packageId->name : L"";
            result.packageIdentityPublisher = packageId->publisher ? packageId->publisher : L"";
            result.packageIdentityPublisherId = packageId->publisherId ? packageId->publisherId : L"";
            result.packageIdentityResourceId = packageId->resourceId ? packageId->resourceId : L"";
        }
    }
    if (!result.packageFullName.empty())
    {
        const auto cacheKey = PackageMetadataCacheKey(result.packageFullName);
        auto found = packageCache_.find(cacheKey);
        if (found == packageCache_.end())
            found = packageCache_.emplace(cacheKey,
                BuildPackageMetadata(result.packageFullName)).first;
        result.package = found->second;
        result.packageMetadataAvailable = true;
        result.microsoftGameConfigs = result.package.configLocations;
        for (auto& location : result.microsoftGameConfigs)
        {
            if (!location.readable || result.executableName.empty())
                continue;
            location.executableMatchEvaluated = true;
            location.currentExecutableMatched = std::any_of(
                location.config.executables.begin(), location.config.executables.end(),
                [&](const auto& executable)
                { return WindowsExecutableNamesMatch(result.executableName, executable.name); });
        }
    }
    return result;
}
}
