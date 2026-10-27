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
    // No supported active display: remove stale result so IPC/UI exposes no VRR outcome.
    static void Clear();
    static std::optional<IntelVrrRunResult> Load();
    static void SetDataDirectoryOverrideForTests(const std::wstring& directory);
};
}
