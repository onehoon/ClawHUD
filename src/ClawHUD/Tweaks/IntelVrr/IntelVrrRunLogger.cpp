#include "IntelVrrRunLogger.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <chrono>

namespace clawhud
{
namespace { std::wstring FilePath() { PWSTR value{}; if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value))) return {}; std::wstring path(value); CoTaskMemFree(value); path += L"\\ClawHUD\\logs"; CreateDirectoryW((path.substr(0, path.find_last_of(L'\\'))).c_str(), nullptr); CreateDirectoryW(path.c_str(), nullptr); return path + L"\\intel-vrr-range-fix.log"; } }
void IntelVrrRunLogger::StartSession() { try { std::wofstream out(FilePath(), std::ios::trunc); out << L"ClawHUD Intel VRR Range Fix - startup session\n"; } catch (...) {} }
void IntelVrrRunLogger::AppendAttempt(int attempt, const std::vector<std::string>& lines) { try { std::wofstream out(FilePath(), std::ios::app); out << L"\n--- Attempt " << attempt << L" ---\n"; for (const auto& line : lines) out << std::wstring(line.begin(), line.end()) << L"\n"; } catch (...) {} }
}
