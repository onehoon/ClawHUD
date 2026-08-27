#include "IntelVrrRunLogger.h"
#include "../../RuntimeLogger.h"
#include <windows.h>
#include <fstream>
#include <chrono>
#include <filesystem>

namespace clawhud
{
namespace { std::filesystem::path FilePath() { return clawhud::LogDirectory() / L"intel-vrr-range-fix.log"; } }
void IntelVrrRunLogger::StartSession() { try { std::wofstream out(FilePath(), std::ios::trunc); out << L"ClawHUD Intel VRR Range Fix - startup session\n"; } catch (...) {} }
void IntelVrrRunLogger::AppendAttempt(int attempt, const std::vector<std::string>& lines) { try { std::wofstream out(FilePath(), std::ios::app); out << L"\n--- Attempt " << attempt << L" ---\n"; for (const auto& line : lines) out << std::wstring(line.begin(), line.end()) << L"\n"; } catch (...) {} }
}
