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
#include <cstring>
#include <limits>
#include <regex>
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

std::wstring Escape(std::wstring_view value)
{
    return EscapeWindowsIdentityDiagnosticValue(value);
}

std::wstring RemoveXmlComments(std::wstring_view xml)
{
    std::wstring result;
    size_t cursor = 0;
    while (cursor < xml.size())
    {
        const size_t begin = xml.find(L"<!--", cursor);
        if (begin == std::wstring_view::npos)
        {
            result.append(xml.substr(cursor));
            break;
        }
        result.append(xml.substr(cursor, begin - cursor));
        const size_t end = xml.find(L"-->", begin + 4);
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

using GetPackagePathByFullName2Fn = LONG (WINAPI*)(PCWSTR, PackagePathType, UINT32*, PWSTR);
using GetPackageInfo2Fn = LONG (WINAPI*)(PACKAGE_INFO_REFERENCE, UINT32, PackagePathType,
    UINT32*, BYTE*, UINT32*);
using GetStagedPackageOriginFn = LONG (WINAPI*)(PCWSTR, PackageOrigin*);

template <typename Function>
Function ResolveKernelApi(const char* name)
{
    if (HMODULE module = GetModuleHandleW(L"kernelbase.dll"))
    {
        if (auto proc = GetProcAddress(module, name))
            return reinterpret_cast<Function>(proc);
    }
    if (HMODULE module = GetModuleHandleW(L"kernel32.dll"))
    {
        if (auto proc = GetProcAddress(module, name))
            return reinterpret_cast<Function>(proc);
    }
    return nullptr;
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
    case PackagePathType_MachineExternal: return L"machineExternal";
    case PackagePathType_UserExternal: return L"userExternal";
    case PackagePathType_EffectiveExternal: return L"effectiveExternal";
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
    constexpr UINT32 kPackageInfoFlags = PACKAGE_INFORMATION_FULL | PACKAGE_FILTER_HEAD;
    UniquePackageInfo reference;
    metadata.packageInfoResult = OpenPackageInfoByFullName(fullName.c_str(), 0, &reference.value);
    Debug(L"packageInfo.open.result=" + ResultName(metadata.packageInfoResult) +
        L" error=" + ErrorCode(metadata.packageInfoResult));
    if (metadata.packageInfoResult != ERROR_SUCCESS) return;

    const auto originQuery = ResolveKernelApi<GetStagedPackageOriginFn>(
        "GetStagedPackageOrigin");
    if (!originQuery)
    {
        metadata.packageOriginResult = ERROR_PROC_NOT_FOUND;
        Debug(L"packageOrigin.result=SYMBOL_MISSING error=" +
            ErrorCode(ERROR_PROC_NOT_FOUND));
    }
    else
    {
        PackageOrigin origin{};
        metadata.packageOriginResult = originQuery(fullName.c_str(), &origin);
        metadata.packageOrigin = static_cast<UINT32>(origin);
        Debug(L"packageOrigin.result=" + ResultName(metadata.packageOriginResult) +
            L" error=" + ErrorCode(metadata.packageOriginResult) +
            L" value=" + std::to_wstring(metadata.packageOrigin));
    }

    const auto query = ResolveKernelApi<GetPackageInfo2Fn>("GetPackageInfo2");
    if (!query)
    {
        metadata.packageInfo2Result = ERROR_PROC_NOT_FOUND;
        Debug(L"packageInfo.result=SYMBOL_MISSING error=" +
            ErrorCode(ERROR_PROC_NOT_FOUND));
    }
    if (query)
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
            if (result == ERROR_SUCCESS && count == 1)
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
                    L" name=\"" + Escape(info->packageId.name ? info->packageId.name : L"") +
                    L"\" publisher=\"" + Escape(info->packageId.publisher ? info->packageId.publisher : L"") +
                    L"\" publisherId=\"" +
                    Escape(info->packageId.publisherId ? info->packageId.publisherId : L"") +
                    L"\" resourceId=\"" +
                    Escape(info->packageId.resourceId ? info->packageId.resourceId : L"") + L"\"");
            }
        }
        if (result != ERROR_SUCCESS)
            Debug(L"packageInfo.result=" + ResultName(result) + L" error=" + ErrorCode(result));
    }

    UINT32 applicationIdsLength{};
    UINT32 applicationIdCount{};
    LONG applicationIdsResult = GetPackageApplicationIds(reference.value,
        &applicationIdsLength, nullptr, &applicationIdCount);
    if (applicationIdsResult == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<BYTE> applicationIds(applicationIdsLength);
        applicationIdsResult = GetPackageApplicationIds(reference.value,
            &applicationIdsLength, applicationIds.data(), &applicationIdCount);
        if (applicationIdsResult == ERROR_SUCCESS)
        {
            const auto ids = reinterpret_cast<PCWSTR*>(applicationIds.data());
            for (UINT32 index = 0; index < applicationIdCount; ++index)
                Debug(L"packageApplicationId value=\"" +
                    Escape(ids[index] ? ids[index] : L"") + L"\"");
        }
    }
    Debug(L"packageApplicationIds.result=" + ResultName(applicationIdsResult) +
        L" error=" + ErrorCode(applicationIdsResult) + L" count=" +
        std::to_wstring(applicationIdCount));
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
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
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
        std::memcpy(probe.text.data(), bytes.data() + 2,
            bytes.size() - 2);
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

void ProbeGameConfigLocation(PackagePathType type,
    const WindowsGameIdentitySource::PackageStaticMetadata::PackagePathProbe& pathProbe,
    std::vector<WindowsGameIdentitySource::PackageStaticMetadata::GameConfigLocationProbe>& output)
{
    if (pathProbe.result != ERROR_SUCCESS || pathProbe.path.empty()) return;
    if (std::any_of(output.begin(), output.end(), [&](const auto& existing)
        { return _wcsicmp(existing.rootPath.c_str(), pathProbe.path.c_str()) == 0; }))
        return;

    WindowsGameIdentitySource::PackageStaticMetadata::GameConfigLocationProbe probe;
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
    if (leftBase.empty() || rightBase.empty()) return false;
    return leftBase.size() == rightBase.size() &&
        std::equal(leftBase.begin(), leftBase.end(), rightBase.begin(),
            [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

std::wstring PackageMetadataCacheKey(std::wstring_view packageFullName)
{
    return std::wstring(packageFullName);
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
    const Request request{
        nextSequence_.fetch_add(1, std::memory_order_relaxed),
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

void WindowsGameIdentitySource::WorkerMain(std::stop_token stop)
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
        InspectImpl(request.window, request.processId,
            request.sequence, request.eventTickMs);
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
        wchar_t title[1024]{};
        GetWindowTextW(foregroundWindow, title, static_cast<int>(std::size(title)));
        Debug(L"pid=" + std::to_wstring(processId) + L" windowVisible=" +
            std::to_wstring(IsWindowVisible(foregroundWindow) ? 1 : 0) +
            L" title=\"" + Escape(title) + L"\"");
        const auto windowAumid = ReadWindowAumid(foregroundWindow);
        LogAumidParts(L"windowAumid", windowAumid);
    }
    if (!processId)
    {
        Debug(L"process.result=NOT_PRESENT reason=no-foreground-pid error=0");
        return;
    }
    UniqueHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)};
    if (!process.get())
    {
        const DWORD error = GetLastError();
        const wchar_t* state = error == ERROR_INVALID_PARAMETER
            ? L"PROCESS_EXITED"
            : (error == ERROR_ACCESS_DENIED ? L"ACCESS_DENIED" : L"API_FAILED");
        Debug(L"process.result=" + std::wstring(state) + L" error=" +
            ErrorCode(error));
        return;
    }
    wchar_t imagePath[32768]{};
    DWORD imageLength = static_cast<DWORD>(std::size(imagePath));
    std::wstring executable;
    if (QueryFullProcessImageNameW(process.get(), 0, imagePath, &imageLength))
    {
        const std::wstring image(imagePath, imageLength);
        Debug(L"pid=" + std::to_wstring(processId) + L" image=\"" + Escape(image) + L"\"");
        executable = std::filesystem::path(image).filename().wstring();
        Debug(L"pid=" + std::to_wstring(processId) + L" executable=\"" + Escape(executable) + L"\"");
    }
    else
    {
        Debug(L"process.image.result=API_FAILED error=" + ErrorCode(GetLastError()));
    }
    {
        LONG resultCode{};
        const auto processAumid = QueryProcessString(process.get(), GetApplicationUserModelId,
            L"processAumid", resultCode);
        LogAumidParts(L"processAumid", processAumid);
        const auto packageFullName = QueryProcessString(process.get(), GetPackageFullName,
            L"packageFullName", resultCode);
        QueryProcessString(process.get(), GetPackageFamilyName, L"packageFamilyName", resultCode);
        UINT32 packageIdLength{};
        LONG packageIdResult = GetPackageId(process.get(), &packageIdLength, nullptr);
        if (packageIdResult == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<BYTE> packageIdBuffer(packageIdLength);
            packageIdResult = GetPackageId(process.get(), &packageIdLength,
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
                    L" name=\"" + Escape(packageId->name ? packageId->name : L"") +
                    L"\" publisher=\"" + Escape(packageId->publisher ? packageId->publisher : L"") +
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
        metadata.machineExternalPath = QueryPackagePath(packageFullName, PackagePathType_MachineExternal);
        metadata.userExternalPath = QueryPackagePath(packageFullName, PackagePathType_UserExternal);
        metadata.effectiveExternalPath = QueryPackagePath(packageFullName, PackagePathType_EffectiveExternal);
        QueryPackageInfo(packageFullName, metadata);
        ProbeGameConfigLocation(PackagePathType_Install, metadata.installPath, metadata.configLocations);
        ProbeGameConfigLocation(PackagePathType_Effective, metadata.effectivePath, metadata.configLocations);
        ProbeGameConfigLocation(PackagePathType_Mutable, metadata.mutablePath, metadata.configLocations);
        ProbeGameConfigLocation(PackagePathType_MachineExternal, metadata.machineExternalPath, metadata.configLocations);
        ProbeGameConfigLocation(PackagePathType_UserExternal, metadata.userExternalPath, metadata.configLocations);
        ProbeGameConfigLocation(PackagePathType_EffectiveExternal, metadata.effectiveExternalPath, metadata.configLocations);
        found = packageCache_.emplace(cacheKey, std::move(metadata)).first;
    }
    const auto& metadata = found->second;
    LogPackagePath(PackagePathTypeName(PackagePathType_Install), metadata.installPath);
    LogPackagePath(PackagePathTypeName(PackagePathType_Effective), metadata.effectivePath);
    LogPackagePath(PackagePathTypeName(PackagePathType_Mutable), metadata.mutablePath);
    LogPackagePath(PackagePathTypeName(PackagePathType_MachineExternal), metadata.machineExternalPath);
    LogPackagePath(PackagePathTypeName(PackagePathType_UserExternal), metadata.userExternalPath);
    LogPackagePath(PackagePathTypeName(PackagePathType_EffectiveExternal), metadata.effectiveExternalPath);
    if (metadata.configLocations.empty())
    {
        Debug(L"microsoftGameConfig.probe.result=NOT_ATTEMPTED reason=no-successful-package-path");
        return;
    }
    for (const auto& location : metadata.configLocations)
    {
        const auto type = static_cast<PackagePathType>(location.pathType);
        const auto probeResult = location.probeError != 0 ? L"API_FAILED"
            : (location.exists ? L"SUCCESS" : L"NOT_PRESENT");
        const auto readResult = !location.readAttempted ? L"NOT_ATTEMPTED"
            : (location.readable ? L"SUCCESS" : L"API_FAILED");
        Debug(L"microsoftGameConfig.pathType=" +
            std::wstring(PackagePathTypeName(type)) + L" root=\"" +
            Escape(location.rootPath) + L"\" path=\"" + Escape(location.configPath) +
            L"\" probe.result=" + probeResult + L" probe.error=" +
            std::to_wstring(location.probeError) + L" exists=" +
            std::to_wstring(location.exists ? 1 : 0) + L" read.result=" +
            readResult + L" read.error=" + std::to_wstring(location.readError));
        if (!location.readable) continue;
        Debug(L"storeId=\"" + Escape(location.config.storeId) + L"\" titleId=\"" +
            Escape(location.config.titleId) + L"\" msaAppId=\"" +
            Escape(location.config.msaAppId) + L"\" executableCount=" +
            std::to_wstring(location.config.executables.size()));
        for (const auto& executable : location.config.executables)
        {
            Debug(L"configExecutable name=\"" + Escape(executable.name) + L"\" id=\"" +
                Escape(executable.id) + L"\" targetDeviceFamily=\"" +
                Escape(executable.targetDeviceFamily) + L"\" architecture=\"" +
                Escape(executable.architecture) + L"\"");
        }
        if (executableName.empty())
            Debug(L"currentExecutableMatch.result=NOT_PRESENT reason=image-name-unavailable");
        else
        {
            bool match = false;
            for (const auto& executable : location.config.executables)
                match = match || WindowsExecutableNamesMatch(executableName, executable.name);
            Debug(L"currentExecutableMatch.result=SUCCESS value=" +
                std::to_wstring(match ? 1 : 0));
        }
    }
}
}
