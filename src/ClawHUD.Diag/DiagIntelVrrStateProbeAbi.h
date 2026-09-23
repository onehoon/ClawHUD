#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace diag_igcl_abi
{
struct GenericVoidDatatype
{
    void* pData{};
    std::uint32_t size{};
};

struct DisplayTiming
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint8_t prefixPadding[3]{};
    std::uint64_t pixelClock{};
    std::uint32_t hActive{};
    std::uint32_t vActive{};
    std::uint32_t hTotal{};
    std::uint32_t vTotal{};
    std::uint32_t hBlank{};
    std::uint32_t vBlank{};
    std::uint32_t hSync{};
    std::uint32_t vSync{};
    float refreshRate{};
    std::uint32_t signalStandard{};
    std::uint8_t vicId{};
    std::uint8_t suffixPadding[3]{};
};

struct DisplayProperties
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint8_t prefixPadding[3]{};
    union OsDisplayEncoder
    {
        std::uint32_t windowsDisplayEncoderId;
        GenericVoidDatatype otherPlatformDisplayEncoderId;
    } osDisplayEncoder{};
    std::array<std::uint8_t, 48> fieldsBeforeTiming{};
    DisplayTiming displayTiming{};
    std::array<std::uint32_t, 16> reservedFields{};
};

inline void InitializeDisplayProperties(DisplayProperties& properties) noexcept
{
    properties = {};
    properties.size = sizeof(properties);
    properties.version = 1;
    properties.displayTiming.size = sizeof(properties.displayTiming);
    properties.displayTiming.version = 1;
}

static_assert(sizeof(GenericVoidDatatype) == 16);
static_assert(sizeof(DisplayTiming) == 64);
static_assert(offsetof(DisplayTiming, pixelClock) == 8);
static_assert(offsetof(DisplayTiming, refreshRate) == 48);
static_assert(offsetof(DisplayTiming, signalStandard) == 52);
static_assert(offsetof(DisplayProperties, osDisplayEncoder) == 8);
static_assert(offsetof(DisplayProperties, displayTiming) == 72);
static_assert(sizeof(DisplayProperties) == 200);
}
