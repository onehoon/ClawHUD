#include "HudPresentationContract.h"

#include <iostream>

int main()
{
    const auto contract = clawhud::ProductionHudPresentationContract();
    bool ok = true;
    const auto expect = [&](bool condition, const char* name)
    {
        if (!condition) std::cerr << "FAILED: " << name << '\n';
        ok &= condition;
    };
    expect((contract.windowExStyle & WS_EX_NOREDIRECTIONBITMAP) != 0, "no redirection bitmap");
    expect((contract.windowExStyle & WS_EX_LAYERED) == 0, "layered absent");
    expect((contract.windowExStyle & WS_EX_NOACTIVATE) != 0, "no activate");
    expect((contract.windowExStyle & WS_EX_TOPMOST) != 0, "topmost");
    expect(contract.textureFormat == DXGI_FORMAT_B8G8R8A8_UNORM, "texture format");
    expect(contract.sampleCount == 1, "sample count");
    expect((contract.resourceMiscFlags & D3D11_RESOURCE_MISC_SHARED) != 0, "shared");
    expect((contract.resourceMiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0, "shared nthandle");
    expect((contract.resourceMiscFlags & D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE) != 0, "shared displayable");
    expect(contract.bufferCount == 3, "three buffers");
    expect(contract.alphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED, "premultiplied alpha");
    expect(contract.identityTransform, "identity transform");
    expect(contract.letterboxLeft == 0.0f && contract.letterboxTop == 0.0f &&
        contract.letterboxRight == 0.0f && contract.letterboxBottom == 0.0f, "letterbox margins");
    expect(contract.independentFlipRequired, "independent flip required");
    return ok ? 0 : 1;
}
