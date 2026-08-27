#pragma once

#include <d3d11.h>
#include <dxgi1_3.h>
#include <windows.h>

namespace clawhud
{
struct HudPresentationContract
{
    DWORD windowExStyle{};
    DXGI_FORMAT textureFormat{};
    UINT sampleCount{};
    UINT bufferCount{};
    UINT resourceMiscFlags{};
    DXGI_ALPHA_MODE alphaMode{};
    bool identityTransform{};
    float letterboxLeft{};
    float letterboxTop{};
    float letterboxRight{};
    float letterboxBottom{};
    bool independentFlipRequired{};
};

constexpr HudPresentationContract ProductionHudPresentationContract() noexcept
{
    return {
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
            WS_EX_LAYERED | WS_EX_TOPMOST,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        1,
        3,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
            D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE,
        DXGI_ALPHA_MODE_PREMULTIPLIED,
        true,
        0.0f, 0.0f, 0.0f, 0.0f,
        true
    };
}
}
