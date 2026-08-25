#include "IntelVrrResultStore.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <map>

namespace clawhud
{
namespace
{
std::wstring dataDirectoryOverride;
std::wstring Path()
{
    if (!dataDirectoryOverride.empty()) { CreateDirectoryW(dataDirectoryOverride.c_str(), nullptr); return dataDirectoryOverride + L"\\tweaks-intel-vrr-result.ini"; }
    PWSTR value{}; if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value))) return {};
    std::wstring path(value); CoTaskMemFree(value); path += L"\\ClawHUD"; CreateDirectoryW(path.c_str(), nullptr); return path + L"\\tweaks-intel-vrr-result.ini";
}
std::string Narrow(const std::wstring& value) { if (value.empty()) return {}; int n = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string r(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, r.data(), n, nullptr, nullptr); r.resize(n - 1); return r; }
std::wstring Wide(const std::string& value) { if (value.empty()) return {}; int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0); std::wstring r(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, r.data(), n); r.resize(n - 1); return r; }
void Write(std::wofstream& out, const wchar_t* key, const std::string& value) { out << key << L"=" << Wide(value) << L"\n"; }
std::map<std::wstring, std::wstring> Read(const std::wstring& path)
{
    std::map<std::wstring, std::wstring> values; std::wifstream in(path); std::wstring line; while (std::getline(in, line)) { auto p = line.find(L'='); if (p != std::wstring::npos) values[line.substr(0, p)] = line.substr(p + 1); } return values;
}
}
void IntelVrrResultStore::SetDataDirectoryOverrideForTests(const std::wstring& directory) { dataDirectoryOverride = directory; }
void IntelVrrResultStore::Save(const IntelVrrRunResult& result)
{
    try { const auto path = Path(); if (path.empty()) return; const auto temp = path + L".tmp"; std::wofstream out(temp, std::ios::trunc); if (!out) return;
        out << L"[IntelVrr]\n"; Write(out, L"Status", IntelVrrRunStatusName(result.status)); Write(out, L"PanelName", result.panelName); Write(out, L"RangeBefore", result.rangeBefore); Write(out, L"RangeAfter", result.rangeAfter); Write(out, L"Message", result.message); Write(out, L"Timestamp", result.timestampUtc); out.close(); MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING); } catch (...) {}
}
std::optional<IntelVrrRunResult> IntelVrrResultStore::Load()
{
    try { const auto values = Read(Path()); if (values.empty()) return std::nullopt; IntelVrrRunResult result{}; const auto status = values.find(L"Status"); if (status == values.end()) return std::nullopt;
        const std::string statusText = Narrow(status->second); const IntelVrrRunStatus statuses[] = { IntelVrrRunStatus::Disabled, IntelVrrRunStatus::Unavailable, IntelVrrRunStatus::UnsupportedPanel, IntelVrrRunStatus::AmbiguousDisplay, IntelVrrRunStatus::AlreadyCorrect, IntelVrrRunStatus::SkippedUserProfile, IntelVrrRunStatus::Applied, IntelVrrRunStatus::ApplyFailed, IntelVrrRunStatus::VerificationFailed };
        bool found = false; for (auto s : statuses) if (statusText == IntelVrrRunStatusName(s)) { result.status = s; found = true; break; } if (!found) return std::nullopt;
        auto get = [&](const wchar_t* key) { auto i = values.find(key); return i == values.end() ? std::string{} : Narrow(i->second); }; result.panelName = get(L"PanelName"); result.rangeBefore = get(L"RangeBefore"); result.rangeAfter = get(L"RangeAfter"); result.message = get(L"Message"); result.timestampUtc = get(L"Timestamp"); return result; } catch (...) { return std::nullopt; }
}
}
