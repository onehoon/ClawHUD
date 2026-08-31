#pragma once

#include <windows.h>

#include "PresentMonApi2Api.h"

#include <cstdint>
#include <string>
#include <vector>

std::string Api2DecodeValue(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element);

class Api2Evidence
{
public:
    bool Start(std::string& detail) noexcept;
    void Stop() noexcept;
    void Retire(DWORD processId) noexcept;
    std::string Sample(DWORD processId) noexcept;

private:
    struct State;
    State* state_{};
};
