#include "EcHelperClient.h"

#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <random>

namespace
{
bool CancelAndDrain(HANDLE handle, OVERLAPPED& overlap)
{
    if (!CancelIoEx(handle, &overlap))
    {
        const DWORD cancelError = GetLastError();
        if (cancelError != ERROR_NOT_FOUND) return false;
    }
    DWORD transferred{};
    if (GetOverlappedResult(handle, &overlap, &transferred, TRUE)) return true;
    const DWORD completionError = GetLastError();
    return completionError == ERROR_OPERATION_ABORTED || completionError == ERROR_BROKEN_PIPE;
}

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
    const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
    if (!connected && connectError != ERROR_IO_PENDING && connectError != ERROR_PIPE_CONNECTED)
    {
        Failure(HRESULT_FROM_WIN32(connectError), clawhud::ec::EcFailureStage::Pipe);
        CloseHandle(overlap.hEvent); Close(); return false;
    }
    if (!connected && connectError == ERROR_IO_PENDING)
    {
        const DWORD wait = WaitForSingleObject(overlap.hEvent, 30000);
        if (wait != WAIT_OBJECT_0)
        {
            const DWORD waitError = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            CancelAndDrain(pipe_, overlap);
            Failure(HRESULT_FROM_WIN32(waitError), clawhud::ec::EcFailureStage::Pipe);
            CloseHandle(overlap.hEvent); Close(); return false;
        }
        DWORD transferred{};
        if (!GetOverlappedResult(pipe_, &overlap, &transferred, FALSE))
        {
            const DWORD error = GetLastError(); Failure(HRESULT_FROM_WIN32(error), clawhud::ec::EcFailureStage::Pipe);
            CloseHandle(overlap.hEvent); Close(); return false;
        }
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
    auto* cursor = static_cast<const std::uint8_t*>(data); DWORD remaining = size;
    while (remaining != 0)
    {
        OVERLAPPED overlap{}; overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlap.hEvent) { Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); return false; }
        DWORD written{}; const BOOL result = WriteFile(pipe_, cursor, remaining, &written, &overlap);
        const DWORD writeError = result ? ERROR_SUCCESS : GetLastError();
        if (!result && writeError != ERROR_IO_PENDING)
        { Failure(HRESULT_FROM_WIN32(writeError), clawhud::ec::EcFailureStage::Pipe); CloseHandle(overlap.hEvent); return false; }
        if (!result)
        {
            const DWORD wait = WaitForSingleObject(overlap.hEvent, 5000);
            if (wait != WAIT_OBJECT_0)
            {
                const DWORD waitError = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
                CancelAndDrain(pipe_, overlap);
                Failure(HRESULT_FROM_WIN32(waitError), clawhud::ec::EcFailureStage::Pipe);
                CloseHandle(overlap.hEvent); return false;
            }
            if (!GetOverlappedResult(pipe_, &overlap, &written, FALSE))
            { const DWORD error = GetLastError(); Failure(HRESULT_FROM_WIN32(error), clawhud::ec::EcFailureStage::Pipe); CloseHandle(overlap.hEvent); return false; }
        }
        CloseHandle(overlap.hEvent); if (!written) { Failure(E_FAIL, clawhud::ec::EcFailureStage::Pipe); return false; }
        cursor += written; remaining -= written;
    }
    return true;
}

bool EcHelperClient::ReadAll(void* data, DWORD size)
{
    auto* cursor = static_cast<std::uint8_t*>(data); DWORD remaining = size;
    while (remaining != 0)
    {
        OVERLAPPED overlap{}; overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlap.hEvent) { Failure(HRESULT_FROM_WIN32(GetLastError()), clawhud::ec::EcFailureStage::Pipe); return false; }
        DWORD read{}; const BOOL result = ReadFile(pipe_, cursor, remaining, &read, &overlap);
        const DWORD readError = result ? ERROR_SUCCESS : GetLastError();
        if (!result && readError != ERROR_IO_PENDING)
        { Failure(HRESULT_FROM_WIN32(readError), clawhud::ec::EcFailureStage::Pipe); CloseHandle(overlap.hEvent); return false; }
        if (!result)
        {
            const DWORD wait = WaitForSingleObject(overlap.hEvent, 30000);
            if (wait != WAIT_OBJECT_0)
            {
                const DWORD waitError = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
                CancelAndDrain(pipe_, overlap);
                Failure(HRESULT_FROM_WIN32(waitError), clawhud::ec::EcFailureStage::Pipe);
                CloseHandle(overlap.hEvent); return false;
            }
            if (!GetOverlappedResult(pipe_, &overlap, &read, FALSE))
            { const DWORD error = GetLastError(); Failure(HRESULT_FROM_WIN32(error), clawhud::ec::EcFailureStage::Pipe); CloseHandle(overlap.hEvent); return false; }
        }
        CloseHandle(overlap.hEvent); if (!read) { Failure(HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE), clawhud::ec::EcFailureStage::Pipe); return false; }
        cursor += read; remaining -= read;
    }
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
