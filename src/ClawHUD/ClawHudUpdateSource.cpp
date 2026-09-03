#include "ClawHudUpdateSource.h"

#include "RuntimeLogger.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <climits>
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

void LogUpdateFailure(const wchar_t* stage, std::string_view what) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Warn,
        std::wstring(L"Velopack: update source ") + stage +
        L" unavailable; continuing installed version (" +
        std::wstring(what.begin(), what.end()) + L")");
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

std::optional<std::filesystem::path> VeloPackUtf8Path(std::string_view utf8) noexcept
{
    if (utf8.empty())
        return std::filesystem::path{};
    if (utf8.size() > static_cast<std::size_t>(INT_MAX))
        return std::nullopt;
    const int size = static_cast<int>(utf8.size());
    const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), size, nullptr, 0);
    if (chars <= 0)
        return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size,
            wide.data(), chars) != chars)
        return std::nullopt;
    return std::filesystem::path(std::move(wide));
}

void RemovePartialDownloadBestEffort(std::string_view utf8Path) noexcept
{
    try
    {
        const auto path = VeloPackUtf8Path(utf8Path);
        if (!path || path->empty())
            return;
        std::error_code ec;
        std::filesystem::remove(*path, ec);
    }
    catch (...)
    {
    }
}

// --- callback-facing overrides: never let an exception reach VeloPack --------

const std::string ClawHudUpdateSource::GetReleaseFeed(
    const std::string releasesName)
{
    try
    {
        return GetReleaseFeedImpl(releasesName);
    }
    catch (const std::exception& ex)
    {
        LogUpdateFailure(L"release-feed", ex.what());
    }
    catch (...)
    {
        LogUpdateFailure(L"release-feed", "unknown error");
    }
    // Non-null empty string: Rust fails JSON parsing -> vpkc_check_for_updates
    // returns UPDATE_ERROR -> UpdateManager::CheckForUpdates() throws
    // -> App::CheckForUpdates() catch continues on the installed version.
    return {};
}

bool ClawHudUpdateSource::DownloadReleaseEntry(
    const Velopack::VelopackAsset& asset, const std::string localFilePath,
    Velopack::vpkc_progress_send_t progress)
{
    try
    {
        DownloadReleaseEntryImpl(asset, localFilePath, progress);
        return true;
    }
    catch (const std::exception& ex)
    {
        RemovePartialDownloadBestEffort(localFilePath);
        LogUpdateFailure(L"package", ex.what());
    }
    catch (...)
    {
        RemovePartialDownloadBestEffort(localFilePath);
        LogUpdateFailure(L"package", "unknown error");
    }
    return false;
}

// --- throwing implementation ------------------------------------------------

std::string ClawHudUpdateSource::GetReleaseFeedImpl(
    const std::string& releasesName)
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

void ClawHudUpdateSource::DownloadReleaseEntryImpl(
    const Velopack::VelopackAsset& asset, const std::string& localFilePath,
    const Velopack::vpkc_progress_send_t& progress)
{
    const auto url = BuildPackageUrl(asset.Version, asset.FileName);
    if (!url)
        throw std::runtime_error("update asset failed validation");

    // VeloPack hands the staging path as UTF-8; decode to the native Windows
    // path so non-ASCII profile directories work. Fail fast on a bad
    // destination before spending a network round trip.
    const auto destination = VeloPackUtf8Path(localFilePath);
    if (!destination || destination->empty())
        throw std::runtime_error("update package destination is not valid UTF-8");
    std::ofstream out(*destination, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("update package destination is not writable");

    Response response;
    OpenBoundedGet(response, *url);
    const auto total = ContentLength(response.request);

    std::uint64_t written{};
    std::int16_t lastReported = -1;
    std::array<char, 64 * 1024> buffer{};
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
    out.close();
    if (progress)
        progress(100);
}
}
