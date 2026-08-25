#pragma once

#include <cstdint>
#include <cstddef>

namespace clawhud::ec
{
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kMaxPayload = 128;

enum class EcOperation : std::uint8_t
{
    GetTemperature = 1,
    GetFan = 2,
    GetData = 3,
};

enum class EcFailureStage : std::uint8_t
{
    None,
    CoInitialize,
    ConnectWmi,
    GetClass,
    GetMethod,
    SpawnInput,
    GetInputData,
    GetWmiFallback,
    CreateSafeArray,
    AccessSafeArray,
    PutBytes,
    PutInputData,
    ExecMethod,
    GetOutputData,
    GetOutputBytes,
    InvalidResponse,
    InvalidSuccessFlag,
    HelperNotElevated,
    Pipe,
    HelperLaunch,
    HelperMissing,
};

#pragma pack(push, 1)
struct EcRequest
{
    std::uint32_t version;
    EcOperation operation;
    std::uint8_t selector;
    std::uint16_t reserved;
};

struct EcResponse
{
    std::uint32_t version;
    std::uint8_t success;
    std::int32_t hresult;
    EcFailureStage stage;
    std::uint32_t payloadLength;
    std::uint8_t payload[kMaxPayload];
};
#pragma pack(pop)

static_assert(sizeof(EcRequest) == 8);
static_assert(sizeof(EcResponse) == 142);

inline bool IsAllowedRequest(const EcRequest& request) noexcept
{
    if (request.version != kProtocolVersion || request.reserved != 0)
        return false;
    switch (request.operation)
    {
    case EcOperation::GetTemperature: return request.selector == 0;
    case EcOperation::GetFan: return request.selector == 0;
    case EcOperation::GetData:
        return request.selector == 70 || request.selector == 71 || request.selector == 74 ||
               request.selector == 75 || request.selector == 221;
    }
    return false;
}
}
