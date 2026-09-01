#include "KnownGameProcessCache.h"

namespace clawhud
{
bool IsKnownGameEvidence(const KnownGameEvidence& evidence) noexcept
{
    return evidence.microsoftGameIdentity || evidence.rendererVerified;
}

template <typename Marker>
void KnownGameProcessCache::Mark(const GameProcessInstance& process,
    Marker mark) noexcept
{
    try
    {
        auto found = entries_.find(process.processId);
        if (found == entries_.end() || found->second.process != process)
            found = entries_.insert_or_assign(process.processId,
                Entry{process, {}}).first;
        mark(found->second.evidence);
    }
    catch (...)
    {
        // Evidence caching is an optimization; allocation failure is a cache miss.
    }
}

void KnownGameProcessCache::MarkMicrosoftGame(
    const GameProcessInstance& process) noexcept
{
    Mark(process, [](KnownGameEvidence& evidence)
        { evidence.microsoftGameIdentity = true; });
}

void KnownGameProcessCache::MarkRendererVerified(
    const GameProcessInstance& process) noexcept
{
    Mark(process, [](KnownGameEvidence& evidence)
        { evidence.rendererVerified = true; });
}

void KnownGameProcessCache::MarkObservedDuringSteamSession(
    const GameProcessInstance& process) noexcept
{
    Mark(process, [](KnownGameEvidence& evidence)
        { evidence.observedDuringSteamSession = true; });
}

std::optional<KnownGameEvidence> KnownGameProcessCache::Lookup(
    const GameProcessInstance& process) noexcept
{
    const auto found = entries_.find(process.processId);
    if (found == entries_.end())
        return std::nullopt;
    if (found->second.process != process)
    {
        entries_.erase(found);
        return std::nullopt;
    }
    return found->second.evidence;
}

bool KnownGameProcessCache::IsKnownGame(
    const GameProcessInstance& process) noexcept
{
    const auto evidence = Lookup(process);
    return evidence && IsKnownGameEvidence(*evidence);
}

void KnownGameProcessCache::Remove(
    const GameProcessInstance& process) noexcept
{
    const auto found = entries_.find(process.processId);
    if (found != entries_.end() && found->second.process == process)
        entries_.erase(found);
}

void KnownGameProcessCache::Clear() noexcept
{
    entries_.clear();
}
}
