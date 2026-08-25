#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

namespace clawhud
{
struct IntelGraphicsApiState
{
    std::uint32_t rawMask{};
    bool dx9{};
    bool dx11{};
    bool dx12{};
    bool vulkan{};
};

IntelGraphicsApiState DecodeGraphicsApiMask(std::uint32_t rawMask) noexcept;
std::optional<std::wstring> ResolveGraphicsApi(
    const IntelGraphicsApiState& state);

class IntelGraphicsApiProbe
{
public:
    ~IntelGraphicsApiProbe();

    std::optional<std::wstring> Query(DWORD processId);

private:
    bool Initialize();
    void Shutdown() noexcept;

    HMODULE library_{};
    void* apiHandle_{};
};
}
