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
};

struct MicrosoftGameConfigSnapshot
{
    bool wellFormed{};
    std::wstring storeId;
    std::wstring titleId;
    std::wstring msaAppId;
    std::vector<MicrosoftGameExecutable> executables;
    std::wstring targetDeviceFamily;
};

MicrosoftGameConfigSnapshot ParseMicrosoftGameConfig(std::wstring_view xml);
bool WindowsExecutableNamesMatch(std::wstring_view left, std::wstring_view right) noexcept;
std::wstring PackageMetadataCacheKey(std::wstring_view packageFullName);

class WindowsGameIdentitySource
{
public:
    void Inspect(HWND foregroundWindow, DWORD processId);

private:
    struct PackageStaticMetadata
    {
        std::wstring packagePath;
        MicrosoftGameConfigSnapshot config;
        bool configExists{};
        bool configReadable{};
    };

    void InspectPackage(const std::wstring& packageFullName,
        const std::wstring& executableName);
    std::unordered_map<std::wstring, PackageStaticMetadata> packageCache_;
    HWND lastWindow_{};
    DWORD lastProcessId_{};
};
}
