#include "RuntimeControlPipeSecurity.h"

#include <sddl.h>

#include <vector>

namespace clawhud::control
{
std::optional<DWORD> CurrentProcessSessionId()
{
    DWORD sessionId{};
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId))
        return std::nullopt;
    return sessionId;
}

std::optional<std::wstring> ControlPipeName()
{
    const auto sessionId = CurrentProcessSessionId();
    if (!sessionId)
        return std::nullopt;
    return L"\\\\.\\pipe\\ClawHUD.Control." + std::to_wstring(*sessionId);
}

std::optional<std::wstring> CurrentUserSidString()
{
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return std::nullopt;

    DWORD length{};
    GetTokenInformation(token, TokenUser, nullptr, 0, &length);
    if (length == 0)
    {
        CloseHandle(token);
        return std::nullopt;
    }

    std::vector<BYTE> buffer(length);
    const BOOL ok = GetTokenInformation(token, TokenUser, buffer.data(), length, &length);
    CloseHandle(token);
    if (!ok)
        return std::nullopt;

    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(user->User.Sid, &sid))
        return std::nullopt;
    std::wstring result(sid);
    LocalFree(sid);
    return result;
}

ControlPipeSecurity::~ControlPipeSecurity()
{
    if (descriptor_)
        LocalFree(descriptor_);
}

bool ControlPipeSecurity::Build()
{
    if (descriptor_) // already built for this process user
        return true;

    const auto sid = CurrentUserSidString();
    if (!sid)
        return false;

    // Protected DACL: only the current user, full access, no inheritance.
    sddl_ = L"D:P(A;;GA;;;" + *sid + L")";

    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl_.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        return false;

    descriptor_ = descriptor;
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = descriptor_;
    attributes_.bInheritHandle = FALSE;
    return true;
}
}
