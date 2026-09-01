#pragma once

#include <windows.h>

#include <optional>

namespace clawhud
{
struct GameProcessInstance
{
    DWORD processId{};
    ULONGLONG creationTime{};

    friend bool operator==(const GameProcessInstance&,
        const GameProcessInstance&) = default;
};

std::optional<GameProcessInstance> QueryGameProcessInstance(
    DWORD processId) noexcept;
}
