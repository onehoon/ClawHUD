#include "MsiEcReader.h"

#include <windows.h>

#include <array>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
bool Elevated()
{
    HANDLE token{}; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION value{}; DWORD size{}; const bool result = GetTokenInformation(token, TokenElevation, &value, sizeof(value), &size) != FALSE;
    CloseHandle(token); return result && value.TokenIsElevated != 0;
}

bool ReadExactSync(HANDLE pipe, void* data, DWORD size)
{
    auto* cursor = static_cast<std::uint8_t*>(data); DWORD remaining = size;
    while (remaining != 0)
    {
        DWORD read{};
        if (!ReadFile(pipe, cursor, remaining, &read, nullptr) || !read) return false;
        cursor += read; remaining -= read;
    }
    return true;
}

bool WriteExactSync(HANDLE pipe, const void* data, DWORD size)
{
    auto* cursor = static_cast<const std::uint8_t*>(data); DWORD remaining = size;
    while (remaining != 0)
    {
        DWORD written{};
        if (!WriteFile(pipe, cursor, remaining, &written, nullptr) || !written) return false;
        cursor += written; remaining -= written;
    }
    return true;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    std::wstring pipeName = commandLine ? commandLine : L"";
    if (pipeName.size() >= 2 && pipeName.front() == L'"' && pipeName.back() == L'"')
        pipeName = pipeName.substr(1, pipeName.size() - 2);
    if (pipeName.empty()) return 2;
    HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 3;
    MsiEcReader reader;
    if (!Elevated())
    {
        clawhud::ec::EcResponse response{ clawhud::ec::kProtocolVersion, 0, E_ACCESSDENIED,
            clawhud::ec::EcFailureStage::HelperNotElevated, 0, {} };
        WriteExactSync(pipe, &response, sizeof(response)); CloseHandle(pipe); return 4;
    }
    if (!reader.Initialize())
    {
        clawhud::ec::EcResponse response{ clawhud::ec::kProtocolVersion, 0, reader.LastError(), reader.LastStage(), 0, {} };
        WriteExactSync(pipe, &response, sizeof(response)); CloseHandle(pipe); return 5;
    }
    for (;;)
    {
        clawhud::ec::EcRequest request{};
        if (!ReadExactSync(pipe, &request, sizeof(request))) break;
        clawhud::ec::EcResponse response{ clawhud::ec::kProtocolVersion, 0, E_INVALIDARG,
            clawhud::ec::EcFailureStage::InvalidResponse, 0, {} };
        if (clawhud::ec::IsAllowedRequest(request))
        {
            std::vector<std::uint8_t> payload;
            if (reader.Read(request.operation, request.selector, payload) && payload.size() <= clawhud::ec::kMaxPayload)
            {
                response.success = 1; response.hresult = S_OK; response.stage = clawhud::ec::EcFailureStage::None;
                response.payloadLength = static_cast<std::uint32_t>(payload.size());
                std::copy(payload.begin(), payload.end(), response.payload);
            }
            else { response.hresult = reader.LastError(); response.stage = reader.LastStage(); }
        }
        if (!WriteExactSync(pipe, &response, sizeof(response))) break;
    }
    reader.Shutdown(); FlushFileBuffers(pipe); CloseHandle(pipe); return 0;
}
