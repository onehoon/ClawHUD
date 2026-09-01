#pragma once

#include "GameProcessInstance.h"

#include <optional>
#include <unordered_map>

namespace clawhud
{
struct KnownGameEvidence
{
    bool microsoftGameIdentity{};
    bool rendererVerified{};
    bool observedDuringSteamSession{};
};

bool IsKnownGameEvidence(const KnownGameEvidence& evidence) noexcept;

class KnownGameProcessCache
{
public:
    // Owned and mutated by GameSessionController's message-handling path.
    void MarkMicrosoftGame(const GameProcessInstance& process) noexcept;
    void MarkRendererVerified(const GameProcessInstance& process) noexcept;
    // Late async completions must not replace a newer generation for the PID.
    bool TryMarkRendererVerified(
        const GameProcessInstance& process) noexcept;
    void MarkObservedDuringSteamSession(
        const GameProcessInstance& process) noexcept;

    std::optional<KnownGameEvidence> Lookup(
        const GameProcessInstance& process) noexcept;
    bool IsKnownGame(const GameProcessInstance& process) noexcept;

    void Remove(const GameProcessInstance& process) noexcept;
    void Clear() noexcept;

private:
    struct Entry
    {
        GameProcessInstance process;
        KnownGameEvidence evidence;
    };

    template <typename Mark>
    void Mark(const GameProcessInstance& process, Mark mark) noexcept;

    std::unordered_map<DWORD, Entry> entries_;
};
}
