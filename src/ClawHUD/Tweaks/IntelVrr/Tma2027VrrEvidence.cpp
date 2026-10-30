#include "Tma2027VrrEvidence.h"

#include <windows.h>
#include <bcrypt.h>
#include <d3dkmthk.h>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace clawhud
{
namespace
{
constexpr wchar_t kTma2027DevicePrefix[] = L"display\\tma2027\\";
constexpr char kHexDigits[] = "0123456789ABCDEF";
constexpr std::size_t kIntelVrrPayloadSize = 124;
constexpr std::uint32_t kIntelDisplayEscapeMajor = 1;
constexpr std::uint32_t kIntelVrrEscapeMinor = 10;
constexpr std::uint32_t kIntelVrrStatusOperation = 0;

class RegistryKey
{
public:
    RegistryKey() = default;
    ~RegistryKey() { if (key_) RegCloseKey(key_); }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    HKEY Get() const noexcept { return key_; }
    HKEY* Put() noexcept { return &key_; }

private:
    HKEY key_{};
};

class BCryptAlgorithmProvider
{
public:
    ~BCryptAlgorithmProvider()
    {
        if (handle_) BCryptCloseAlgorithmProvider(handle_, 0);
    }
    BCRYPT_ALG_HANDLE* Put() noexcept { return &handle_; }
    BCRYPT_ALG_HANDLE Get() const noexcept { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_{};
};

class BCryptHashHandle
{
public:
    ~BCryptHashHandle()
    {
        if (handle_) BCryptDestroyHash(handle_);
    }
    BCRYPT_HASH_HANDLE* Put() noexcept { return &handle_; }
    BCRYPT_HASH_HANDLE Get() const noexcept { return handle_; }

private:
    BCRYPT_HASH_HANDLE handle_{};
};

class DynamicLibrary
{
public:
    explicit DynamicLibrary(HMODULE handle) noexcept : handle_(handle) {}
    ~DynamicLibrary() { if (handle_) FreeLibrary(handle_); }
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    HMODULE Get() const noexcept { return handle_; }

private:
    HMODULE handle_{};
};

class DeviceContext
{
public:
    explicit DeviceContext(HDC handle) noexcept : handle_(handle) {}
    ~DeviceContext() { if (handle_) DeleteDC(handle_); }
    DeviceContext(const DeviceContext&) = delete;
    DeviceContext& operator=(const DeviceContext&) = delete;
    HDC Get() const noexcept { return handle_; }

private:
    HDC handle_{};
};

class D3dkmtAdapter
{
public:
    D3dkmtAdapter(D3DKMT_HANDLE handle, PFND3DKMT_CLOSEADAPTER close) noexcept
        : handle_(handle), close_(close) {}
    ~D3dkmtAdapter() { Close(); }
    D3dkmtAdapter(const D3dkmtAdapter&) = delete;
    D3dkmtAdapter& operator=(const D3dkmtAdapter&) = delete;
    D3DKMT_HANDLE Get() const noexcept { return handle_; }
    NTSTATUS Close() noexcept
    {
        if (!handle_ || !close_) return 0;
        D3DKMT_CLOSEADAPTER args{};
        args.hAdapter = handle_;
        const auto status = close_(&args);
        handle_ = 0;
        return status;
    }

private:
    D3DKMT_HANDLE handle_{};
    PFND3DKMT_CLOSEADAPTER close_{};
};

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

bool EqualsInsensitiveAscii(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        auto leftCharacter = left[index];
        auto rightCharacter = right[index];
        if (leftCharacter >= 'A' && leftCharacter <= 'Z')
            leftCharacter = static_cast<char>(leftCharacter + ('a' - 'A'));
        if (rightCharacter >= 'A' && rightCharacter <= 'Z')
            rightCharacter = static_cast<char>(rightCharacter + ('a' - 'A'));
        if (leftCharacter != rightCharacter) return false;
    }
    return true;
}

bool IsTma2027PanelIdentity(const Tma2027EvidenceRequest& request) noexcept
{
    return EqualsInsensitiveAscii(request.manufacturer, "TMA") &&
        EqualsInsensitiveAscii(request.productCode, "2027") &&
        EqualsInsensitiveAscii(request.panelName, "TL070FVXS02-0");
}

bool IsSafeTmaInstancePath(std::wstring_view normalizedPath) noexcept
{
    constexpr auto prefixLength = std::size(kTma2027DevicePrefix) - 1;
    if (normalizedPath.size() <= prefixLength ||
        CompareStringOrdinal(normalizedPath.data(), static_cast<int>(prefixLength),
            kTma2027DevicePrefix, static_cast<int>(prefixLength), TRUE) != CSTR_EQUAL)
        return false;

    const auto instance = normalizedPath.substr(prefixLength);
    if (instance.empty() || instance.find(L'\\') != std::wstring_view::npos)
        return false;
    return std::all_of(instance.begin(), instance.end(), [](wchar_t character)
    {
        return (character >= L'a' && character <= L'z') ||
            (character >= L'0' && character <= L'9') ||
            character == L'&' || character == L'_' || character == L'-';
    });
}

enum class OverrideReadResult
{
    None,
    Present,
    Unavailable,
};

OverrideReadResult ReadOverrideState(HKEY deviceParameters)
{
    RegistryKey overrideKey;
    const auto open = RegOpenKeyExW(deviceParameters, L"EDID_OVERRIDE", 0,
        KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, overrideKey.Put());
    if (open == ERROR_FILE_NOT_FOUND)
        return OverrideReadResult::None;
    if (open != ERROR_SUCCESS)
        return OverrideReadResult::Unavailable;

    // The presence of the override key itself is sufficient to fail closed.
    // It may be empty at the point we inspect it while an override is staged.
    return OverrideReadResult::Present;
}

std::optional<std::vector<std::uint8_t>> ReadReportedEdid(HKEY deviceParameters)
{
    DWORD type{};
    DWORD byteCount{};
    if (RegQueryValueExW(deviceParameters, L"EDID", nullptr, &type, nullptr,
            &byteCount) != ERROR_SUCCESS || type != REG_BINARY ||
        (byteCount != 128 && byteCount != 256))
        return std::nullopt;

    std::vector<std::uint8_t> bytes(byteCount);
    DWORD actualByteCount = byteCount;
    if (RegQueryValueExW(deviceParameters, L"EDID", nullptr, &type,
            bytes.data(), &actualByteCount) != ERROR_SUCCESS ||
        type != REG_BINARY || actualByteCount != byteCount)
        return std::nullopt;
    return bytes;
}

Tma2027DirectRange QueryDirectIntelRange(const Tma2027EvidenceRequest& request)
{
    Tma2027DirectRange failure;
    failure.attempted = true;
    if (request.sourceDeviceName.empty())
    {
        failure.failureDetail = "missing Windows source device name";
        return failure;
    }

    DynamicLibrary gdi32(LoadLibraryExW(L"gdi32.dll", nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!gdi32.Get())
    {
        failure.win32Error = GetLastError();
        failure.failureDetail = "LoadLibraryExW(gdi32.dll)";
        return failure;
    }
    const auto openAdapter = reinterpret_cast<PFND3DKMT_OPENADAPTERFROMHDC>(
        GetProcAddress(gdi32.Get(), "D3DKMTOpenAdapterFromHdc"));
    const auto closeAdapter = reinterpret_cast<PFND3DKMT_CLOSEADAPTER>(
        GetProcAddress(gdi32.Get(), "D3DKMTCloseAdapter"));
    const auto escape = reinterpret_cast<PFND3DKMT_ESCAPE>(
        GetProcAddress(gdi32.Get(), "D3DKMTEscape"));
    if (!openAdapter || !closeAdapter || !escape)
    {
        failure.failureDetail = "required gdi32 D3DKMT export unavailable";
        return failure;
    }

    DeviceContext dc(CreateDCW(nullptr, request.sourceDeviceName.c_str(), nullptr, nullptr));
    if (!dc.Get())
    {
        failure.win32Error = GetLastError();
        failure.failureDetail = "CreateDCW for exact Windows source";
        return failure;
    }

    D3DKMT_OPENADAPTERFROMHDC opened{};
    opened.hDc = dc.Get();
    const auto openStatus = openAdapter(&opened);
    if (openStatus != 0 || opened.hAdapter == 0)
    {
        failure.ntStatus = static_cast<std::int32_t>(openStatus);
        failure.failureDetail = "D3DKMTOpenAdapterFromHdc";
        return failure;
    }
    D3dkmtAdapter adapter(opened.hAdapter, closeAdapter);

    failure.adapterLuidHighPart = opened.AdapterLuid.HighPart;
    failure.adapterLuidLowPart = opened.AdapterLuid.LowPart;
    failure.sourceId = opened.VidPnSourceId;
    const LUID expectedLuid{ request.adapterLuidLowPart,
        request.adapterLuidHighPart };
    if (!SameLuid(opened.AdapterLuid, expectedLuid) ||
        opened.VidPnSourceId != request.sourceId)
    {
        failure.failureDetail = "opened adapter/source does not match exact Windows target";
        return failure;
    }

    // Intel's read-only status operation is operation 0. The 124-byte private
    // payload follows the Zendo ClawLab driver-interface contract: major/minor/
    // operation at 8/12/16, supported and VRR-enabled bytes at 20/21, then
    // display count, target ID, and physical min/max Hz at 24/28/32/36.
    // No setter operation or LFC flag is issued here. The returned target ID
    // must match the exact QueryDisplayConfig target before the range can
    // authorize a TMA exception.
    std::array<std::uint8_t, kIntelVrrPayloadSize> payload{};
    const auto write32 = [&](std::size_t offset, std::uint32_t value)
    {
        payload[offset] = static_cast<std::uint8_t>(value);
        payload[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        payload[offset + 2] = static_cast<std::uint8_t>(value >> 16);
        payload[offset + 3] = static_cast<std::uint8_t>(value >> 24);
    };
    write32(8, kIntelDisplayEscapeMajor);
    write32(12, kIntelVrrEscapeMinor);
    write32(16, kIntelVrrStatusOperation);

    D3DKMT_ESCAPE args{};
    args.hAdapter = adapter.Get();
    args.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    args.pPrivateDriverData = payload.data();
    args.PrivateDriverDataSize = static_cast<UINT>(payload.size());
    const auto escapeStatus = escape(&args);
    auto result = DecodeTma2027DirectRange(static_cast<std::int32_t>(escapeStatus),
        payload, opened.AdapterLuid.HighPart, opened.AdapterLuid.LowPart,
        opened.VidPnSourceId);
    result.adapterAndSourceMatch = true;
    if (escapeStatus != 0)
        result.failureDetail = "D3DKMTEscape read-only VRR status operation";
    else if (!result.available)
        result.failureDetail = "Intel direct status returned no active target";
    else if (result.targetId != request.targetId)
    {
        result.available = false;
        result.failureDetail = "Intel direct status target ID does not match QueryDisplayConfig";
    }

    const auto closeStatus = adapter.Close();
    if (closeStatus != 0 && result.failureDetail.empty())
    {
        result.available = false;
        result.ntStatus = static_cast<std::int32_t>(closeStatus);
        result.failureDetail = "D3DKMTCloseAdapter";
    }
    return result;
}
}

std::optional<std::string> ComputeTma2027EdidSha256(
    std::span<const std::uint8_t> bytes) noexcept
{
    try
    {
        BCryptAlgorithmProvider algorithm;
        BCryptHashHandle hash;
        std::vector<UCHAR> hashObject;
        std::array<UCHAR, 32> digest{};
        ULONG objectSize{};
        ULONG returnedSize{};

        if (BCryptOpenAlgorithmProvider(algorithm.Put(), BCRYPT_SHA256_ALGORITHM,
                nullptr, 0) < 0)
            return std::nullopt;
        if (BCryptGetProperty(algorithm.Get(), BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                &returnedSize, 0) < 0 || objectSize == 0 ||
            bytes.size() > std::numeric_limits<ULONG>::max())
            return std::nullopt;
        hashObject.resize(objectSize);
        if (BCryptCreateHash(algorithm.Get(), hash.Put(), hashObject.data(),
                objectSize, nullptr, 0, 0) < 0 || !hash.Get())
            return std::nullopt;
        if (BCryptHashData(hash.Get(), const_cast<PUCHAR>(bytes.data()),
                static_cast<ULONG>(bytes.size()), 0) < 0 ||
            BCryptFinishHash(hash.Get(), digest.data(),
                static_cast<ULONG>(digest.size()), 0) < 0)
            return std::nullopt;

        std::string output(digest.size() * 2, '\0');
        for (std::size_t index = 0; index < digest.size(); ++index)
        {
            output[index * 2] = kHexDigits[digest[index] >> 4];
            output[index * 2 + 1] = kHexDigits[digest[index] & 0x0f];
        }
        return output;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

Tma2027Evidence CollectTma2027Evidence(const Tma2027EvidenceRequest& request,
    bool queryDirectRange) noexcept
{
    Tma2027Evidence evidence;
    try
    {
        evidence.target = { request.adapterLuidHighPart, request.adapterLuidLowPart,
            request.sourceId, request.targetId, request.normalizedTargetInstance };
        if (!IsTma2027PanelIdentity(request) || !request.targetAvailable ||
            !IsSafeTmaInstancePath(request.normalizedMonitorInstance) ||
            !IsSafeTmaInstancePath(request.normalizedTargetInstance) ||
            CompareStringOrdinal(request.normalizedMonitorInstance.data(),
                static_cast<int>(request.normalizedMonitorInstance.size()),
                request.normalizedTargetInstance.data(),
                static_cast<int>(request.normalizedTargetInstance.size()), TRUE) != CSTR_EQUAL ||
            !IsSafeTmaInstancePath(evidence.target.normalizedMonitorInstance))
        {
            evidence.edidStatus = Tma2027EdidStatus::WrongTarget;
            evidence.directRange.failureDetail = queryDirectRange
                ? "exact TMA2027 target binding failed" : "not queried";
            evidence.directRange.attempted = queryDirectRange;
            return evidence;
        }

        const std::wstring deviceParametersPath = L"SYSTEM\\CurrentControlSet\\Enum\\" +
            request.normalizedMonitorInstance + L"\\Device Parameters";
        RegistryKey deviceParameters;
        const auto open = RegOpenKeyExW(HKEY_LOCAL_MACHINE, deviceParametersPath.c_str(),
            0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, deviceParameters.Put());
        if (open != ERROR_SUCCESS)
        {
            evidence.edidStatus = Tma2027EdidStatus::Unavailable;
            evidence.directRange.failureDetail = "could not open exact monitor Device Parameters key";
        }
        else
        {
            const auto override = ReadOverrideState(deviceParameters.Get());
            if (override == OverrideReadResult::Present)
                evidence.edidStatus = Tma2027EdidStatus::OverridePresent;
            else if (override == OverrideReadResult::Unavailable)
                evidence.edidStatus = Tma2027EdidStatus::Unavailable;
            else
            {
                const auto rawEdid = ReadReportedEdid(deviceParameters.Get());
                if (!rawEdid)
                    evidence.edidStatus = Tma2027EdidStatus::Unavailable;
                else
                {
                    const auto canonical = CanonicalizeTma2027Edid(*rawEdid);
                    if (!canonical)
                        evidence.edidStatus = Tma2027EdidStatus::Mismatch;
                    else
                    {
                        const auto hash = ComputeTma2027EdidSha256(*canonical);
                        if (!hash)
                            evidence.edidStatus = Tma2027EdidStatus::Unavailable;
                        else
                        {
                            evidence.edidSha256 = *hash;
                            evidence.edidStatus = IsTma2027PhysicalEdidHash(*hash)
                                ? Tma2027EdidStatus::Matched : Tma2027EdidStatus::Mismatch;
                        }
                    }
                }
            }
        }

        if (queryDirectRange)
            evidence.directRange = QueryDirectIntelRange(request);
        else
            evidence.directRange.failureDetail = "not required for the reported capability";
    }
    catch (...)
    {
        evidence.edidStatus = Tma2027EdidStatus::Unavailable;
        evidence.directRange.failureDetail = "exception while collecting read-only TMA evidence";
        evidence.directRange.attempted = queryDirectRange;
    }
    return evidence;
}

const char* Tma2027EdidStatusName(Tma2027EdidStatus status) noexcept
{
    switch (status)
    {
    case Tma2027EdidStatus::Matched: return "MATCHED";
    case Tma2027EdidStatus::Mismatch: return "MISMATCH";
    case Tma2027EdidStatus::Unavailable: return "UNAVAILABLE";
    case Tma2027EdidStatus::OverridePresent: return "OVERRIDE_PRESENT";
    case Tma2027EdidStatus::WrongTarget: return "WRONG_TARGET";
    default: return "NOT_CHECKED";
    }
}

const char* Tma2027DirectRangeStatusName(const Tma2027DirectRange& range) noexcept
{
    if (!range.attempted) return "NOT_REQUIRED";
    if (!range.available || !range.adapterAndSourceMatch || !range.supported ||
        !range.vrrEnabled || range.minimumHz == 0 || range.maximumHz == 0)
        return "UNAVAILABLE";
    return "AVAILABLE";
}
}
