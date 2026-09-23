#ifdef NDEBUG
#undef NDEBUG
#endif

#include "DiagIntelVrrStateProbe.h"

#include <cassert>
#include <array>

int main()
{
    const std::array<std::uint32_t, 3> ids{ 17, 0, 8 };
    const auto exact = ResolveDiagIgclTargetId(0, ids);
    assert(exact.status == DiagIgclTargetMappingStatus::Exact);
    assert(exact.outputIndex == 1);

    const auto missing = ResolveDiagIgclTargetId(4, ids);
    assert(missing.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!missing.outputIndex);

    const std::array<std::uint32_t, 3> duplicateIds{ 17, 8, 17 };
    const auto ambiguous = ResolveDiagIgclTargetId(17, duplicateIds);
    assert(ambiguous.status == DiagIgclTargetMappingStatus::Ambiguous);
    assert(!ambiguous.outputIndex);

    const auto incomplete = ResolveDiagIgclTargetId(8, ids, false);
    assert(incomplete.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!incomplete.outputIndex);

    const auto incompleteDuplicate = ResolveDiagIgclTargetId(17, duplicateIds, false);
    assert(incompleteDuplicate.status == DiagIgclTargetMappingStatus::Ambiguous);
    assert(!incompleteDuplicate.outputIndex);
}
