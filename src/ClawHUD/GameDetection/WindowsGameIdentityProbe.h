#pragma once

#include <windows.h>
#include <appmodel.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace clawhud
{
struct MicrosoftGameExecutable
{
    std::wstring name;
    std::wstring id;
    std::wstring targetDeviceFamily;
    std::wstring architecture;
};

struct MicrosoftGameConfigSnapshot
{
    bool recognizedGameRoot{};
    std::wstring storeId;
    std::wstring titleId;
    std::wstring msaAppId;
    std::vector<MicrosoftGameExecutable> executables;
};

MicrosoftGameConfigSnapshot ParseMicrosoftGameConfig(std::wstring_view xml);
bool WindowsExecutableNamesMatch(std::wstring_view left, std::wstring_view right) noexcept;
std::wstring PackageMetadataCacheKey(std::wstring_view packageFullName);

struct MicrosoftGameConfigProbeResult
{
    int pathType{};
    std::wstring rootPath;
    std::wstring configPath;
    bool probeAttempted{};
    bool exists{};
    int probeError{};
    bool readAttempted{};
    bool readable{};
    DWORD readError{};
    MicrosoftGameConfigSnapshot config;
    bool executableMatchEvaluated{};
    bool currentExecutableMatched{};
};

struct WindowsPackagePathProbe
{
    LONG result{ERROR_SUCCESS};
    std::wstring path;
};

struct WindowsPackageStaticMetadata
{
    WindowsPackagePathProbe installPath;
    WindowsPackagePathProbe effectivePath;
    WindowsPackagePathProbe mutablePath;
    WindowsPackagePathProbe machineExternalPath;
    WindowsPackagePathProbe userExternalPath;
    WindowsPackagePathProbe effectiveExternalPath;
    LONG packageInfoResult{ERROR_SUCCESS};
    bool packageInfo2Attempted{};
    LONG packageInfo2Result{ERROR_SUCCESS};
    UINT32 packageInfoCount{};
    UINT32 packageInfoFlags{};
    std::wstring packageInfoPath;
    std::wstring packageInfoFullName;
    std::wstring packageInfoFamilyName;
    std::wstring packageInfoName;
    std::wstring packageInfoPublisher;
    std::wstring packageInfoPublisherId;
    std::wstring packageInfoResourceId;
    UINT32 packageInfoArchitecture{};
    PACKAGE_VERSION packageInfoVersion{};
    std::vector<std::wstring> packageApplicationIds;
    bool packageApplicationIdsAttempted{};
    LONG packageApplicationIdsResult{ERROR_SUCCESS};
    UINT32 packageApplicationIdCount{};
    std::vector<MicrosoftGameConfigProbeResult> configLocations;
    bool packageOriginAttempted{};
    LONG packageOriginResult{ERROR_SUCCESS};
    UINT32 packageOrigin{};
};

struct WindowsGameIdentityProbeResult
{
    DWORD processId{};
    bool processOpened{};
    DWORD processOpenError{};
    std::wstring imagePath;
    DWORD imagePathError{};
    std::wstring executableName;
    LONG processAumidResult{ERROR_SUCCESS};
    std::wstring processAumid;
    LONG packageFullNameResult{ERROR_SUCCESS};
    std::wstring packageFullName;
    LONG packageFamilyNameResult{ERROR_SUCCESS};
    std::wstring packageFamilyName;
    LONG packageIdentityResult{ERROR_SUCCESS};
    UINT32 packageIdentityArchitecture{};
    PACKAGE_VERSION packageIdentityVersion{};
    std::wstring packageIdentityName;
    std::wstring packageIdentityPublisher;
    std::wstring packageIdentityPublisherId;
    std::wstring packageIdentityResourceId;
    WindowsPackageStaticMetadata package;
    bool packageMetadataAvailable{};
    std::vector<MicrosoftGameConfigProbeResult> microsoftGameConfigs;
};

bool HasReadableMicrosoftGameExecutableMatch(
    const WindowsGameIdentityProbeResult& result) noexcept;

class WindowsGameIdentityProbe
{
public:
    WindowsGameIdentityProbeResult Inspect(DWORD processId);

private:
    std::unordered_map<std::wstring, WindowsPackageStaticMetadata> packageCache_;
};
}
