#include "EcHelperClient.h"

#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <random>

namespace
{
std::wstring PipeName()
{
    std::random_device random;
    return L"\\.\\pipe\\ClawHUD.Ec." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(static_cast<unsigned long long>(random()) ^ GetTickCount64());
}
}

EcHelperClient::~EcHelperClient() { Close(); }

void EcHelperClient::Failure(HRESULT error, clawhud::ec::EcFailureStage stage)
{
    error_ = error;
    stage_ = stage;
}

bool EcHelperClient::EnsureConnected()
{
    if (Connected()) return true;
    if (attempted_) return false;
    attempted_ = true;
    if (pipe_ != INVALID_HANDLE_VALUE) Close();

    pipeName_ = PipeName();
    pipe_ = CreateNamedPipeW(pipeName_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, sizeof(clawhud::ec::EcResponse),
        sizeof(clawhud::ec::EcRequest), 0, nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) { Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); return false; }
    if (!StartHelper(pipeName_)) { Close(); return false; }

    OVERLAPPED overlap{}; overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlap.hEvent) { Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); Close(); return false; }
    const BOOL connected = ConnectNamedPipe(pipe_, &overlap);
    if (!connected && GetLastError() != ERROR_IO_PENDING && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe);
        CloseHandle(overlap.hEvent); Close(); return false;
    }
    if (!connected && GetLastError() == ERROR_IO_PENDING && WaitForSingleObject(overlap.hEvent, 30000) != WAIT_OBJECT_0)
    {
        Failure(HRESULT_FROM_WIN32(ERROR_TIMEOUT), clawhud::ec::EcFailureStage::Pipe);
        CancelIo(pipe_); CloseHandle(overlap.hEvent); Close(); return false;
    }
    CloseHandle(overlap.hEvent); stage_ = clawhud::ec::EcFailureStage::None; error_ = S_OK; return true;
}

bool EcHelperClient::StartHelper(const std::wstring& pipeName)
{
    wchar_t module[MAX_PATH]{}; const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (!length) { Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::HelperLaunch); return false; }
    const auto helper = std::filesystem::path(module).parent_path() / L"ClawHUD.EcHelper.exe";
    if (!std::filesystem::exists(helper)) { Failure(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), clawhud::ec::EcFailureStage::HelperMissing); return false; }
    std::wstring parameters = pipeName;
    SHELLEXECUTEINFOW info{ sizeof(info) }; info.fMask = SEE_MASK_NOCLOSEPROCESS; info.lpVerb = L"runas";
    info.lpFile = helper.c_str(); info.lpParameters = parameters.c_str(); info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) { Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::HelperLaunch); return false; }
    helperProcess_ = info.hProcess; helperPid_ = GetProcessId(helperProcess_); return true;
}

bool EcHelperClient::WriteAll(const void* data, DWORD size)
{
    OVERLAPPED overlap{}; overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD written{}; const BOOL result = WriteFile(pipe_, data, size, &written, &overlap);
    if (!result && GetLastError() != ERROR_IO_PENDING) { CloseHandle(overlap.hEvent); Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); return false; }
    if (!result && (WaitForSingleObject(overlap.hEvent, 5000) != WAIT_OBJECT_0 || !GetOverlappedResult(pipe_, &overlap, &written, FALSE)))
    { CloseHandle(overlap.hEvent); Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); return false; }
    CloseHandle(overlap.hEvent); if (written != size) { Failure(E_FAIL, clawhud::ec::EcFailureStage::Pipe); return false; }
    return true;
}

bool EcHelperClient::ReadAll(void* data, DWORD size)
{
    OVERLAPPED overlap{}; overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD read{}; const BOOL result = ReadFile(pipe_, data, size, &read, &overlap);
    if (!result && GetLastError() != ERROR_IO_PENDING) { CloseHandle(overlap.hEvent); Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); return false; }
    if (!result && (WaitForSingleObject(overlap.hEvent, 30000) != WAIT_OBJECT_0 || !GetOverlappedResult(pipe_, &overlap, &read, FALSE)))
    { CloseHandle(overlap.hEvent); Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); return false; }
    CloseHandle(overlap.hEvent); if (read != size) { Failure(HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE), clawhud::ec::EcFailureStage::Pipe); return false; }
    return true;
}

bool EcHelperClient::Send(clawhud::ec::EcOperation operation, std::uint8_t selector,
    std::vector<std::uint8_t>& payload)
{
    payload.clear(); if (!EnsureConnected()) return false;
    clawhud::ec::EcRequest request{ clawhud::ec::kProtocolVersion, operation, selector, 0 };
    if (!WriteAll(&request, sizeof(request))) { Close(); return false; }
    clawhud::ec::EcResponse response{};
    if (!ReadAll(&response, sizeof(response))) { Close(); return false; }
    if (response.version != clawhud::ec::kProtocolVersion || response.payloadLength > clawhud::ec::kMaxPayload)
    { Failure(E_INVALIDARG, clawhud::ec::EcFailureStage::InvalidResponse); return false; }
    error_ = static_cast<HRESULT>(response.hresult); stage_ = response.stage;
    helperElevated_ = stage_ != clawhud::ec::EcFailureStage::HelperNotElevated;
    if (!response.success) { if (stage_ == clawhud::ec::EcFailureStage::None) stage_ = clawhud::ec::EcFailureStage::InvalidSuccessFlag; return false; }
    payload.assign(response.payload, response.payload + response.payloadLength); return true;
}

bool EcHelperClient::ReadTemperature(std::vector<std::uint8_t>& payload) { return Send(clawhud::ec::EcOperation::GetTemperature, 0, payload); }
bool EcHelperClient::ReadFan(std::vector<std::uint8_t>& payload) { return Send(clawhud::ec::EcOperation::GetFan, 0, payload); }
bool EcHelperClient::ReadData(std::uint8_t selector, std::vector<std::uint8_t>& payload) { return Send(clawhud::ec::EcOperation::GetData, selector, payload); }

void EcHelperClient::Close()
{
    if (pipe_ != INVALID_HANDLE_VALUE) { FlushFileBuffers(pipe_); DisconnectNamedPipe(pipe_); CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; }
    if (helperProcess_) { CloseHandle(helperProcess_); helperProcess_ = {}; }
    helperPid_ = 0; pipeName_.clear(); helperElevated_ = false;
}
