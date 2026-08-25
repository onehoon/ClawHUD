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

void WriteResponse(HANDLE pipe, const clawhud::ec::EcResponse& response)
{
    DWORD written{}; WriteFile(pipe, &response, sizeof(response), &written, nullptr);
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
        WriteResponse(pipe, response); CloseHandle(pipe); return 4;
    }
    if (!reader.Initialize())
    {
        clawhud::ec::EcResponse response{ clawhud::ec::kProtocolVersion, 0, reader.LastError(), reader.LastStage(), 0, {} };
        WriteResponse(pipe, response); CloseHandle(pipe); return 5;
    }
    for (;;)
    {
        clawhud::ec::EcRequest request{}; DWORD read{};
        if (!ReadFile(pipe, &request, sizeof(request), &read, nullptr) || read == 0) break;
        clawhud::ec::EcResponse response{ clawhud::ec::kProtocolVersion, 0, E_INVALIDARG,
            clawhud::ec::EcFailureStage::InvalidResponse, 0, {} };
        if (read == sizeof(request) && clawhud::ec::IsAllowedRequest(request))
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
        WriteResponse(pipe, response);
    }
    reader.Shutdown(); FlushFileBuffers(pipe); CloseHandle(pipe); return 0;
}
