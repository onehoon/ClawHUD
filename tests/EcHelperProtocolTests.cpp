#include "EcHelperProtocol.h"

#include <cassert>
#include <array>

int main()
{
    using namespace clawhud::ec;
    assert(IsAllowedRequest({ kProtocolVersion, EcOperation::GetTemperature, 0, 0 }));
    assert(IsAllowedRequest({ kProtocolVersion, EcOperation::GetFan, 0, 0 }));
    for (const auto selector : std::array{ 70, 71, 74, 75, 221 })
        assert(IsAllowedRequest({ kProtocolVersion, EcOperation::GetData, static_cast<std::uint8_t>(selector), 0 }));
    assert(!IsAllowedRequest({ kProtocolVersion, EcOperation::GetTemperature, 1, 0 }));
    assert(!IsAllowedRequest({ kProtocolVersion, EcOperation::GetFan, 1, 0 }));
    assert(!IsAllowedRequest({ kProtocolVersion, EcOperation::GetData, 1, 0 }));
    assert(!IsAllowedRequest({ 2, EcOperation::GetFan, 0, 0 }));
    assert(!IsAllowedRequest({ kProtocolVersion, static_cast<EcOperation>(99), 0, 0 }));
    return 0;
}
