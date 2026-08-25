#pragma once

#include <string>
#include <vector>

namespace clawhud
{
class IntelVrrRunLogger
{
public:
    static void StartSession();
    static void AppendAttempt(int attempt, const std::vector<std::string>& lines);
};
}
