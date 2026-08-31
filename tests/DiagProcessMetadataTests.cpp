#include "DiagnosticSession.h"

#include <cassert>
#include <filesystem>

int main()
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    assert(process);
    const auto image = DiagnosticQueryProcessImagePath(process);
    CloseHandle(process);
    assert(!image.empty());
    assert(std::filesystem::path(image).extension() == L".exe");
}
