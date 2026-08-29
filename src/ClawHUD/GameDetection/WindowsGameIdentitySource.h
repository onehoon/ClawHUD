#pragma once

#include <windows.h>

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

class WindowsGameIdentitySource
{
public:
    void Inspect(HWND foregroundWindow, DWORD processId) noexcept;

public:
    struct PackageStaticMetadata
    {
        struct PackagePathProbe
        {
            LONG result{ERROR_SUCCESS};
            std::wstring path;
        };

        PackagePathProbe installPath;
        PackagePathProbe effectivePath;
        PackagePathProbe mutablePath;
        PackagePathProbe machineExternalPath;
        PackagePathProbe userExternalPath;
        PackagePathProbe effectiveExternalPath;
        LONG packageInfoResult{ERROR_SUCCESS};
        LONG packageInfo2Result{ERROR_SUCCESS};
        UINT32 packageInfoFlags{};
        MicrosoftGameConfigSnapshot config;
        std::wstring configPath;
        bool configProbeAttempted{};
        bool configExists{};
        bool configReadAttempted{};
        bool configReadable{};
        int configProbeError{};
        DWORD configReadError{};
    };

private:
    void InspectImpl(HWND foregroundWindow, DWORD processId);
    void InspectPackage(const std::wstring& packageFullName,
        const std::wstring& executableName);
    std::unordered_map<std::wstring, PackageStaticMetadata> packageCache_;
    HWND lastWindow_{};
    DWORD lastProcessId_{};
};
}
