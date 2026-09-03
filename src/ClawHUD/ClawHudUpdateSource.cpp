#include "ClawHudUpdateSource.h"

#include "RuntimeLogger.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace clawhud
{
namespace
{
void LogUpdate(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Info, L"Velopack: " + message);
}

struct Response
{
    HINTERNET session{};
    HINTERNET connect{};
    HINTERNET request{};
    ~Response()
    {
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
    Response() = default;
    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;
};

// Opens a bounded https GET to `url`, follows https redirects, and requires a
// 2xx final status. Throws std::runtime_error on any failure or timeout.
void OpenBoundedGet(Response& r, const std::wstring& url)
{
    r.session = WinHttpOpen(L"ClawHUD-Updater",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!r.session)
        throw std::runtime_error("WinHttpOpen failed");
    WinHttpSetTimeouts(r.session, kUpdateResolveTimeoutMs,
        kUpdateConnectTimeoutMs, kUpdateSendTimeoutMs, kUpdateReceiveTimeoutMs);

    std::array<wchar_t, 256> host{};
    std::array<wchar_t, 2048> path{};
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.lpszHostName = host.data();
    components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data();
    components.dwUrlPathLength = static_cast<DWORD>(path.size());
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components) ||
        components.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("update URL is not valid https");

    r.connect = WinHttpConnect(r.session, host.data(), components.nPort, 0);
    if (!r.connect)
        throw std::runtime_error("WinHttpConnect failed");

    r.request = WinHttpOpenRequest(r.connect, L"GET", path.data(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r.request)
        throw std::runtime_error("WinHttpOpenRequest failed");
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(r.request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
        sizeof(redirectPolicy));

    if (!WinHttpSendRequest(r.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r.request, nullptr))
        throw std::runtime_error("update request failed within timeout");

    DWORD status{};
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(r.request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("update response had no status code");
    if (status < 200 || status >= 300)
        throw std::runtime_error("update endpoint returned HTTP " +
            std::to_string(status));
}

std::optional<std::uint64_t> ContentLength(HINTERNET request)
{
    std::uint64_t value{};
    DWORD size = sizeof(value);
    if (WinHttpQueryHeaders(request,
            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
            WINHTTP_HEADER_NAME_BY_INDEX, &value, &size, WINHTTP_NO_HEADER_INDEX))
        return value;
    return std::nullopt;
}
}

const std::string ClawHudUpdateSource::GetReleaseFeed(
    const std::string releasesName)
{
    const auto url = BuildReleaseFeedUrl(releasesName);
    if (!url)
        throw std::runtime_error("unsupported release feed name: " + releasesName);
    LogUpdate(L"checking stable release feed source=github-release-bounded");

    Response response;
    OpenBoundedGet(response, *url);
    std::string body;
    for (;;)
    {
        DWORD available{};
        if (!WinHttpQueryDataAvailable(response.request, &available))
            throw std::runtime_error("release feed read failed within timeout");
        if (available == 0)
            break;
        const std::size_t offset = body.size();
        if (offset + available > kUpdateFeedMaxBytes)
            throw std::runtime_error("release feed exceeds size ceiling");
        body.resize(offset + available);
        DWORD read{};
        if (!WinHttpReadData(response.request, body.data() + offset, available,
                &read))
            throw std::runtime_error("release feed read failed within timeout");
        body.resize(offset + read);
        if (read == 0)
            break;
    }
    return body;
}

bool ClawHudUpdateSource::DownloadReleaseEntry(
    const Velopack::VelopackAsset& asset, const std::string localFilePath,
    Velopack::vpkc_progress_send_t progress)
{
    const auto url = BuildPackageUrl(asset.Version, asset.FileName);
    if (!url)
        throw std::runtime_error("update asset failed validation");

    Response response;
    OpenBoundedGet(response, *url);
    const auto total = ContentLength(response.request);

    const std::filesystem::path destination(
        std::filesystem::path(localFilePath).make_preferred());
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("update package destination is not writable");

    std::uint64_t written{};
    std::int16_t lastReported = -1;
    std::array<char, 64 * 1024> buffer{};
    try
    {
        for (;;)
        {
            DWORD available{};
            if (!WinHttpQueryDataAvailable(response.request, &available))
                throw std::runtime_error("update download stalled past timeout");
            if (available == 0)
                break;
            while (available > 0)
            {
                const DWORD want = available < buffer.size()
                    ? available
                    : static_cast<DWORD>(buffer.size());
                DWORD read{};
                if (!WinHttpReadData(response.request, buffer.data(), want, &read))
                    throw std::runtime_error("update download stalled past timeout");
                if (read == 0)
                {
                    available = 0;
                    break;
                }
                out.write(buffer.data(), static_cast<std::streamsize>(read));
                if (!out)
                    throw std::runtime_error("update package write failed");
                written += read;
                available -= read;
                if (progress && total && *total > 0)
                {
                    const auto percent = static_cast<std::int16_t>(
                        (written * 100) / *total);
                    if (percent != lastReported)
                    {
                        lastReported = percent;
                        progress(percent);
                    }
                }
            }
        }
        out.flush();
        if (!out)
            throw std::runtime_error("update package write failed");
    }
    catch (...)
    {
        out.close();
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        throw;
    }
    out.close();
    if (progress)
        progress(100);
    return true;
}
}
