#include "PresentActivitySource.h"

#include "PresentMonDebugFrameTelemetry.h"
#include "PresentMonTelemetryProvider.h"
#include "RuntimeLogger.h"

#include <chrono>
#include <cstdio>
#include <sstream>

namespace clawhud
{
namespace
{
constexpr auto kPollInterval = std::chrono::milliseconds(250);
constexpr auto kLogInterval = std::chrono::milliseconds(1000);

void LogDebug(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Debug, message);
}
}

std::wstring FormatPresentActivityLine(DWORD processId,
    const PresentMonDebugFrame& frame)
{
    std::wstringstream line;
    line << L"[PresentActivity] pid=" << processId;
    if (frame.swapChainAddress)
    {
        wchar_t address[19]{};
        swprintf_s(address, L"0x%016llX",
            static_cast<unsigned long long>(*frame.swapChainAddress));
        line << L" swapChain=" << address;
    }
    if (frame.presentMode)
        line << L" presentMode=" << PresentMonPresentModeName(*frame.presentMode);
    if (frame.frameType)
        line << L" frameType=" << PresentMonFrameTypeName(*frame.frameType);
    if (frame.betweenDisplayChangeMs)
        line << L" betweenDisplayChangeMs=" << *frame.betweenDisplayChangeMs
             << L" displayed=" << (*frame.betweenDisplayChangeMs > 0.0 ? 1 : 0);
    return line.str();
}

PresentActivitySource::~PresentActivitySource()
{
    Stop();
}

void PresentActivitySource::Start(PresentMonTelemetryProvider& provider)
{
    Stop();
    provider_ = &provider;
    stop_.store(false);
    running_.store(true);
    try
    {
        worker_ = std::thread(&PresentActivitySource::PollLoop, this);
    }
    catch (...)
    {
        running_.store(false);
        provider_ = nullptr;
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"[PresentActivity] start.result=FAILED stage=ReaderThread");
    }
}

void PresentActivitySource::Stop() noexcept
{
    const bool had = worker_.joinable();
    stop_.store(true);
    watched_.store(0);
    if (worker_.joinable())
        worker_.join();
    running_.store(false);
    provider_ = nullptr;
    if (had)
        LogDebug(L"[PresentActivity] stop.result=SUCCESS");
}

void PresentActivitySource::Watch(DWORD processId) noexcept
{
    watched_.store(processId);
}

void PresentActivitySource::PollLoop()
{
    DWORD leasedPid = 0;
    PresentMonProcessLease lease;
    auto nextLog = std::chrono::steady_clock::now();

    while (!stop_.load())
    {
        const DWORD pid = watched_.load();
        if (pid != leasedPid)
        {
            lease.Release();
            leasedPid = 0;
            if (pid != 0 && provider_)
            {
                lease = provider_->AcquireProcess(pid);
                if (lease)
                {
                    leasedPid = pid;
                    nextLog = std::chrono::steady_clock::now();
                    LogDebug(L"[PresentActivity] watch pid=" +
                        std::to_wstring(pid));
                }
            }
        }

        if (leasedPid != 0 && provider_)
        {
            const auto frame = provider_->ReadDebugFrameActivity(leasedPid);
            const auto now = std::chrono::steady_clock::now();
            if (frame && now >= nextLog)
            {
                LogDebug(FormatPresentActivityLine(leasedPid, *frame));
                nextLog = now + kLogInterval;
            }
        }

        std::this_thread::sleep_for(kPollInterval);
    }
    lease.Release();
}
}
