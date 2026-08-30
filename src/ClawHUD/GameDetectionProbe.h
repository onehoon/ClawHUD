#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace clawhud
{
struct GameDetectionEngineDelta
{
    DWORD processId{};
    double delta{};
};

struct GameDetectionCandidate
{
    DWORD processId{};
    HWND window{};
    std::wstring executable;
    std::wstring title;
    double gpu3dDelta{};
};

std::optional<DWORD> ParseGpuEngineProcessId(std::wstring_view instance) noexcept;
bool IsGpuEngine3DInstance(std::wstring_view instance) noexcept;
bool IsPresentMonCandidateWindow(bool visible, HWND owner) noexcept;
double PositiveCounterDelta(double first, double second) noexcept;
std::vector<GameDetectionCandidate> RankGpuCandidates(
    const std::vector<GameDetectionEngineDelta>& engines,
    const std::vector<GameDetectionCandidate>& windows);
class PresentMonAutoTargetBlocklist
{
public:
    bool Load(const std::filesystem::path& path);
    bool Contains(std::wstring_view executable) const noexcept;
    std::size_t Size() const noexcept { return blocked_.size(); }

private:
    std::unordered_set<std::wstring> blocked_;
};
std::vector<GameDetectionCandidate> FilterPresentMonAutoTargetCandidates(
    const std::vector<GameDetectionCandidate>& candidates,
    const PresentMonAutoTargetBlocklist& blocklist);
bool IsFullscreenLike(const RECT& window, const RECT& monitor,
    LONG tolerance = 2) noexcept;
std::string FormatProbePid(DWORD processId);
std::string FormatProbeOptional(const std::optional<double>& value);

class GameDetectionProbe
{
public:
    using Api2Summary = std::function<std::string(DWORD)>;

    GameDetectionProbe(std::filesystem::path path, Api2Summary api2Summary = {});
    ~GameDetectionProbe();
    GameDetectionProbe(const GameDetectionProbe&) = delete;
    GameDetectionProbe& operator=(const GameDetectionProbe&) = delete;

    bool Start();
    void Sample(std::int64_t elapsedMs);
    void Stop() noexcept;

private:
    void LogForeground(HWND window, DWORD processId);
    void LogGeometry(HWND window);
    void LogPdhCandidates();
    static std::wstring ProcessName(DWORD processId);
    static std::wstring WindowTitle(HWND window);

    std::filesystem::path path_;
    Api2Summary api2Summary_;
    std::ofstream log_;
    DWORD previousForegroundPid_{};
    std::wstring previousForegroundExe_;
    std::uint64_t sequence_{};
    PresentMonAutoTargetBlocklist blocklist_;
    bool blocklistLoaded_{};
    bool started_{};
};
}
