#include "DiagnosticSession.h"

#include <cassert>
#include <filesystem>

int main()
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    assert(process);
    const auto image = DiagnosticQueryProcessImagePath(process);
    const auto startFileTime = DiagnosticQueryProcessStartFileTime(process);
    CloseHandle(process);
    assert(!image.empty());
    assert(std::filesystem::path(image).extension() == L".exe");

    // Stable process identity: the creation FILETIME must be resolvable so a
    // reused numeric PID cannot merge two process generations.
    assert(startFileTime != 0);

    const DiagProcessKey generationA{ GetCurrentProcessId(), startFileTime };
    const DiagProcessKey generationB{ GetCurrentProcessId(), startFileTime + 1 };
    assert(generationA == generationA);
    assert(!(generationA == generationB));
    assert(DiagProcessKeyHash{}(generationA) != DiagProcessKeyHash{}(generationB));
}
