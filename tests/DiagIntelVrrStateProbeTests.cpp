#ifdef NDEBUG
#undef NDEBUG
#endif

#include "DiagIntelVrrStateProbe.h"

#include <cassert>
#include <array>

int main()
{
    const LUID intel{ 17, 2 };
    const LUID discrete{ 21, 3 };
    const std::array<DiagIgclOutputIdentity, 3> outputs{{
        { intel, 17 }, { discrete, 0 }, { intel, 8 } }};
    const auto exact = ResolveDiagIgclTarget(intel, 8, outputs);
    assert(exact.status == DiagIgclTargetMappingStatus::Exact);
    assert(exact.outputIndex == 2);

    // The numeric target ID can repeat on a different adapter. Its LUID must
    // also match the Windows display path before the output is considered.
    const auto wrongAdapter = ResolveDiagIgclTarget(intel, 0, outputs);
    assert(wrongAdapter.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!wrongAdapter.outputIndex);

    const std::array<DiagIgclOutputIdentity, 3> duplicateOutputs{{
        { intel, 17 }, { discrete, 17 }, { intel, 17 } }};
    const auto ambiguous = ResolveDiagIgclTarget(intel, 17, duplicateOutputs);
    assert(ambiguous.status == DiagIgclTargetMappingStatus::Ambiguous);
    assert(!ambiguous.outputIndex);

    const auto missing = ResolveDiagIgclTarget(intel, 4, outputs);
    assert(missing.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!missing.outputIndex);

    const auto incomplete = ResolveDiagIgclTarget(intel, 8, outputs, false);
    assert(incomplete.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!incomplete.outputIndex);

    const auto incompleteDuplicate = ResolveDiagIgclTarget(intel, 17,
        duplicateOutputs, false);
    assert(incompleteDuplicate.status == DiagIgclTargetMappingStatus::Ambiguous);
    assert(!incompleteDuplicate.outputIndex);
}
