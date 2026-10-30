#include "Tweaks/IntelVrr/Tma2027VrrPolicy.h"
#include "Tweaks/IntelVrr/Tma2027VrrEvidence.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace clawhud;
namespace
{
Tma2027TargetIdentity Target()
{
    return { 3, 303, 3, 33, L"display\\tma2027\\5&3&uid3" };
}

Tma2027ProfileState Profile(std::uint32_t profile, float capMinimum,
    float profileMinimum, float profileMaximum)
{
    return { true, true, capMinimum, 120.0f, true, profile,
        profileMinimum, profileMaximum, Target() };
}

Tma2027Evidence VerifiedEvidence()
{
    const auto target = Target();
    Tma2027Evidence evidence;
    evidence.target = target;
    evidence.edidStatus = Tma2027EdidStatus::Matched;
    evidence.edidSha256 = std::string(kTma2027PhysicalEdidSha256);
    evidence.directRange = { true, true, true, true, true, 0,
        target.adapterLuidHighPart, target.adapterLuidLowPart, target.sourceId,
        target.targetId, 1, 48, 120 };
    return evidence;
}

std::vector<std::uint8_t> ValidEdid()
{
    std::vector<std::uint8_t> bytes(128);
    constexpr std::array<std::uint8_t, 8> header{
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    std::copy(header.begin(), header.end(), bytes.begin());
    bytes[126] = 0;
    std::uint8_t sum{};
    for (std::size_t index = 0; index < 127; ++index)
        sum = static_cast<std::uint8_t>(sum + bytes[index]);
    bytes[127] = static_cast<std::uint8_t>(0u - sum);
    return bytes;
}
}

int main()
{
    auto evidence = VerifiedEvidence();
    auto state = Profile(1, 48.0f, 60.0f, 120.0f);
    assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::Unavailable);
    assert(!IsTma2027ExcellentSetterAllowed(state, nullptr));
    assert(EvaluateTma2027Profile(state, &evidence) == Tma2027Decision::ApplyExcellent);
    assert(IsTma2027ExcellentSetterAllowed(state, &evidence));
    auto untrustedNativeEvidence = evidence;
    untrustedNativeEvidence.edidStatus = Tma2027EdidStatus::OverridePresent;
    assert(EvaluateTma2027Profile(state, &untrustedNativeEvidence) ==
        Tma2027Decision::Unavailable);
    assert(!IsTma2027ExcellentSetterAllowed(state, &untrustedNativeEvidence));
    untrustedNativeEvidence = evidence;
    untrustedNativeEvidence.edidStatus = Tma2027EdidStatus::Mismatch;
    assert(!IsTma2027ExcellentSetterAllowed(state, &untrustedNativeEvidence));
    untrustedNativeEvidence = evidence;
    untrustedNativeEvidence.target.targetId++;
    assert(!IsTma2027ExcellentSetterAllowed(state, &untrustedNativeEvidence));

    state = Profile(4, 48.0f, 48.0f, 120.0f);
    assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::Unavailable);
    assert(EvaluateTma2027Profile(state, &evidence) == Tma2027Decision::AlreadyCorrect);
    assert(!IsTma2027ExcellentSetterAllowed(state, &evidence));

    state = Profile(1, 24.0f, 60.0f, 120.0f);
    assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::Unavailable);
    assert(!IsTma2027ExcellentSetterAllowed(state, nullptr));

    assert(EvaluateTma2027Profile(state, &evidence) == Tma2027Decision::ApplyExcellent);
    assert(IsTma2027ExcellentSetterAllowed(state, &evidence));
    auto readback = Profile(2, 24.0f, 48.0f, 120.0f);
    assert(IsTma2027ExcellentReadbackVerified(readback, &evidence));
    assert(EvaluateTma2027Profile(readback, &evidence) == Tma2027Decision::AlreadyCorrect);
    readback = Profile(2, 48.0f, 48.0f, 120.0f);
    assert(!IsTma2027ExcellentReadbackVerified(readback, nullptr));
    assert(IsTma2027ExcellentReadbackVerified(readback, &evidence));

    auto invalidEvidence = evidence;
    invalidEvidence.directRange.minimumHz = 24;
    assert(!IsTma2027VerifiedHalfFloorEvidence(invalidEvidence, Target()));
    assert(EvaluateTma2027Profile(state, &invalidEvidence) == Tma2027Decision::Unavailable);
    invalidEvidence = evidence;
    invalidEvidence.target.targetId++;
    assert(!IsTma2027VerifiedHalfFloorEvidence(invalidEvidence, Target()));
    invalidEvidence = evidence;
    invalidEvidence.directRange.adapterLuidLowPart++;
    assert(!IsTma2027VerifiedHalfFloorEvidence(invalidEvidence, Target()));
    invalidEvidence = evidence;
    invalidEvidence.directRange.sourceId++;
    assert(!IsTma2027VerifiedHalfFloorEvidence(invalidEvidence, Target()));
    invalidEvidence = evidence;
    invalidEvidence.target.normalizedMonitorInstance = L"display\\tma2027\\5&4&uid4";
    assert(!IsTma2027VerifiedHalfFloorEvidence(invalidEvidence, Target()));
    invalidEvidence = evidence;
    invalidEvidence.directRange.vrrEnabled = false;
    assert(!IsTma2027VerifiedHalfFloorEvidence(invalidEvidence, Target()));
    invalidEvidence = evidence;
    invalidEvidence.edidStatus = Tma2027EdidStatus::OverridePresent;
    assert(EvaluateTma2027Profile(state, &invalidEvidence) == Tma2027Decision::Unavailable);

    for (const auto profile : { 5u, 7u })
    {
        state = Profile(profile, 24.0f, 30.0f, 120.0f);
        assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::PreserveUserProfile);
        assert(!IsTma2027ExcellentSetterAllowed(state, &evidence));
    }
    state = Profile(99, 48.0f, 48.0f, 120.0f);
    assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::Unsupported);
    state = Profile(1, 15.0f, 48.0f, 120.0f);
    assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::Unsupported);
    state = Profile(1, 48.0f, 24.0f, 120.0f);
    assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::Unsupported);
    state = Profile(1, 48.0f, 60.0f, 121.0f);
    assert(EvaluateTma2027Profile(state, nullptr) == Tma2027Decision::Unsupported);

    assert(IsTma2027PhysicalEdidHash(kTma2027PhysicalEdidSha256));
    const std::array<std::uint8_t, 3> hashVector{ 'a', 'b', 'c' };
    const auto knownHash = ComputeTma2027EdidSha256(hashVector);
    assert(knownHash && *knownHash ==
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
    auto lowercaseHash = std::string(kTma2027PhysicalEdidSha256);
    for (auto& character : lowercaseHash)
        if (character >= 'A' && character <= 'F') character = static_cast<char>(character + ('a' - 'A'));
    assert(IsTma2027PhysicalEdidHash(lowercaseHash));
    lowercaseHash[0] = lowercaseHash[0] == '0' ? '1' : '0';
    assert(!IsTma2027PhysicalEdidHash(lowercaseHash));

    auto edid = ValidEdid();
    const auto canonical = CanonicalizeTma2027Edid(edid);
    assert(canonical && *canonical == edid);
    auto padded = edid;
    padded.resize(256, 0);
    const auto paddedCanonical = CanonicalizeTma2027Edid(padded);
    assert(paddedCanonical && *paddedCanonical == edid);
    padded[200] = 1;
    assert(!CanonicalizeTma2027Edid(padded));
    edid[126] = 1;
    assert(!CanonicalizeTma2027Edid(edid));
    edid = ValidEdid();
    edid[127]++;
    assert(!CanonicalizeTma2027Edid(edid));
    edid = ValidEdid();
    edid[0] = 1;
    assert(!CanonicalizeTma2027Edid(edid));
    edid = ValidEdid();
    edid.resize(129);
    assert(!CanonicalizeTma2027Edid(edid));

    std::array<std::uint8_t, 40> directPayload{};
    directPayload[20] = 1;
    directPayload[21] = 1;
    directPayload[24] = 1;
    directPayload[28] = 33;
    directPayload[32] = 48;
    directPayload[36] = 120;
    auto direct = DecodeTma2027DirectRange(0, directPayload,
        Target().adapterLuidHighPart, Target().adapterLuidLowPart, Target().sourceId);
    assert(direct.attempted && direct.available && direct.supported && direct.vrrEnabled);
    assert(direct.targetId == 33 && direct.minimumHz == 48 && direct.maximumHz == 120);
    assert(!DecodeTma2027DirectRange(0xC0000022,
        directPayload, 3, 303, 3).available);
    assert(!DecodeTma2027DirectRange(0, std::span<const std::uint8_t>(directPayload).first(39),
        3, 303, 3).available);

    return 0;
}
