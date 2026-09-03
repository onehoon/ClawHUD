#include "ClawHudUpdateSource.h"

#include <windows.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

// Cleanup 3 review:
//  - the two VeloPack callback-facing overrides must never throw across the
//    C/Rust FFI boundary; forced failures return empty feed / false.
//  - VeloPack passes the staging path as UTF-8; it must be decoded to the native
//    Windows path, not run through the ANSI code page.
// No network is used.

namespace
{
namespace fs = std::filesystem;

// Encode a wide Windows path the way VeloPack's Rust bridge does (UTF-8).
std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        utf8.data(), bytes, nullptr, nullptr);
    return utf8;
}
}

int main()
{
    clawhud::ClawHudUpdateSource source;

    // --- FFI safety: forced failures return normally, never throw ------------

    // Feed: unsupported release name fails before any socket work.
    assert(source.GetReleaseFeed("releases.evil.json").empty());

    // Download: asset fails validation (bad version token + traversal filename).
    {
        Velopack::VelopackAsset bad;
        bad.Version = "not a version";
        bad.FileName = "../../etc/passwd";
        assert(!source.DownloadReleaseEntry(bad, "ignored.nupkg", nullptr));
    }

    // Download: valid asset, unwritable destination (parent dir does not exist).
    // The impl checks the destination before any network round trip.
    {
        Velopack::VelopackAsset asset;
        asset.Version = "0.1.90";
        asset.FileName = "ClawHUD-0.1.90-stable-full.nupkg";
        const auto dest = fs::temp_directory_path() /
            L"clawhud-updatesource-tests-no-such-dir" / L"pkg.nupkg";
        std::error_code ec;
        fs::remove_all(dest.parent_path(), ec);
        assert(!source.DownloadReleaseEntry(asset, WideToUtf8(dest.wstring()),
            nullptr));
        assert(!fs::exists(dest));
    }

    // --- UTF-8 callback path decoding --------------------------------------

    {
        // Hangul via universal character names so the test source stays ASCII:
        // U+D6C8 (hun); U+D328 U+D0A4 U+C9C0 (package).
        const auto dir = fs::temp_directory_path() /
            (L"clawhud-\uD6C8-utf8-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        const auto wideFile = dir / L"\uD328\uD0A4\uC9C0.nupkg";
        const std::string utf8File = WideToUtf8(wideFile.wstring());

        // The production decoder yields exactly the intended UTF-16 path,
        // not an ANSI-mangled one.
        const auto decoded = clawhud::VeloPackUtf8Path(utf8File);
        assert(decoded && decoded->wstring() == wideFile.wstring());

        // Filesystem ops work at that path, and the cleanup helper removes it
        // when given the same UTF-8 string.
        { std::ofstream(*decoded, std::ios::binary) << "partial"; }
        assert(fs::exists(wideFile));
        clawhud::RemovePartialDownloadBestEffort(utf8File);
        assert(!fs::exists(wideFile));

        // Invalid UTF-8 is rejected, not silently misdecoded.
        assert(!clawhud::VeloPackUtf8Path(std::string("\xff\xfe bad")).has_value());
        assert(clawhud::VeloPackUtf8Path("").has_value());  // empty -> empty path

        fs::remove_all(dir, ec);
    }

    return 0;
}
