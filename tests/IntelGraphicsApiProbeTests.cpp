#include "IntelGraphicsApiProbe.h"

#include <iostream>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    ok &= Check(DecodeGraphicsApiMask(0x01).dx9, "DX9 mask");
    ok &= Check(DecodeGraphicsApiMask(0x02).dx11, "DX11 mask");
    ok &= Check(DecodeGraphicsApiMask(0x04).dx12, "DX12 mask");
    ok &= Check(DecodeGraphicsApiMask(0x08).vulkan, "Vulkan mask");
    ok &= Check(ResolveGraphicsApi(DecodeGraphicsApiMask(0x02)) == L"DX11",
        "DX11 resolution");
    ok &= Check(ResolveGraphicsApi(DecodeGraphicsApiMask(0x04)) == L"DX12",
        "DX12 resolution");
    ok &= Check(ResolveGraphicsApi(DecodeGraphicsApiMask(0x08)) == L"Vulkan",
        "Vulkan resolution");
    ok &= Check(!ResolveGraphicsApi(DecodeGraphicsApiMask(0x06)),
        "mixed mask unavailable");
    ok &= Check(!ResolveGraphicsApi(DecodeGraphicsApiMask(0x00)),
        "zero mask unavailable");
    return ok ? 0 : 1;
}
