#pragma once

#include <windows.h>

#include <string>
#include <vector>

class Api2Evidence
{
public:
    bool Start(std::string& detail) noexcept;
    void Stop() noexcept;
    std::string Sample(DWORD processId) noexcept;

private:
    struct State;
    State* state_{};
};
