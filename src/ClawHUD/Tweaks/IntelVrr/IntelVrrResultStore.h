#pragma once

#include "IntelVrrRunResult.h"
#include <optional>
#include <string>

namespace clawhud
{
class IntelVrrResultStore
{
public:
    static void Save(const IntelVrrRunResult& result);
    static std::optional<IntelVrrRunResult> Load();
    static void SetDataDirectoryOverrideForTests(const std::wstring& directory);
};
}
