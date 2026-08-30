#include <Windows.h>

#include <cstdint>
#include <iostream>

struct PM_VERSION {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
    char tag[22];
    char hash[8];
    char config[4];
};

enum PM_STATUS : int {
    PM_STATUS_SUCCESS = 0,
};

using PM_SESSION_HANDLE = void*;
using PmGetApiVersion = PM_STATUS(__cdecl*)(PM_VERSION*);
using PmOpenSession = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE*);
using PmCloseSession = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE);

int main() {
    HMODULE loader = LoadLibraryW(L"PresentMonAPI2Loader.dll");
    if (!loader) {
        std::cerr << "LoadLibraryW(PresentMonAPI2Loader.dll) failed: " << GetLastError() << '\n';
        return 1;
    }

    const auto getApiVersion = reinterpret_cast<PmGetApiVersion>(GetProcAddress(loader, "pmGetApiVersion"));
    const auto openSession = reinterpret_cast<PmOpenSession>(GetProcAddress(loader, "pmOpenSession"));
    const auto closeSession = reinterpret_cast<PmCloseSession>(GetProcAddress(loader, "pmCloseSession"));
    if (!getApiVersion || !openSession || !closeSession) {
        std::cerr << "Required loader export is missing\n";
        FreeLibrary(loader);
        return 2;
    }

    PM_VERSION version{};
    const PM_STATUS versionStatus = getApiVersion(&version);
    std::cout << "pmGetApiVersion status=" << static_cast<int>(versionStatus)
              << " version=" << version.major << '.' << version.minor << '.' << version.patch << '\n';

    PM_SESSION_HANDLE session = nullptr;
    const PM_STATUS openStatus = openSession(&session);
    std::cout << "pmOpenSession status=" << static_cast<int>(openStatus) << '\n';

    PM_STATUS closeStatus = PM_STATUS_SUCCESS;
    if (openStatus == PM_STATUS_SUCCESS && session) {
        closeStatus = closeSession(session);
        std::cout << "pmCloseSession status=" << static_cast<int>(closeStatus) << '\n';
    } else {
        std::cout << "pmCloseSession status=NOT_CALLED\n";
    }

    FreeLibrary(loader);
    return versionStatus == PM_STATUS_SUCCESS && openStatus == PM_STATUS_SUCCESS && closeStatus == PM_STATUS_SUCCESS ? 0 : 3;
}
