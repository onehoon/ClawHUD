#include "WindowsMemoryTelemetry.h"

#include <cstdint>
#include <iostream>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    ok &= Check(UsedPhysicalMemory(32ull * 1024 * 1024 * 1024,
        12ull * 1024 * 1024 * 1024) ==
        20ull * 1024 * 1024 * 1024, "physical memory usage calculation");
    ok &= Check(!UsedPhysicalMemory(12, 32),
        "invalid physical memory availability is omitted");
    ok &= Check(ReadSystemMemoryUsedBytes().has_value(),
        "system memory helper reads current physical memory");
    return ok ? 0 : 1;
}
