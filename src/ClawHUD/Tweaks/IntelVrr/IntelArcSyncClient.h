#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace clawhud
{
enum class IntelArcSyncProfile : std::uint32_t { Invalid, Recommended, Excellent, Good, Compatible, Off, Vesa, Custom, Max };

struct IntelDisplayOutput
{
    void* adapter{};
    void* output{};
    std::wstring friendlyName;
};

struct IntelArcSyncCapability
{
    bool supported{};
    float minimumHz{};
    float maximumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

struct IntelArcSyncProfileState
{
    IntelArcSyncProfile profile{ IntelArcSyncProfile::Invalid };
    float minimumHz{};
    float maximumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

struct IntelArcSyncCall
{
    std::string operation;
    std::uint32_t rawResult{};
    std::string resultName;
    std::string detail;
    std::string ToString() const;
};

class IIntelArcSyncClient
{
public:
    virtual ~IIntelArcSyncClient() = default;
    virtual bool Initialize() = 0;
    virtual std::vector<IntelDisplayOutput> EnumerateDisplayOutputs() = 0;
    virtual bool GetMonitorCapability(const IntelDisplayOutput&, IntelArcSyncCapability&) = 0;
    virtual bool GetArcSyncProfile(const IntelDisplayOutput&, IntelArcSyncProfileState&) = 0;
    virtual bool SetArcSyncProfile(const IntelDisplayOutput&, IntelArcSyncProfile, std::string& error) = 0;
    virtual const std::vector<IntelArcSyncCall>& CallLog() const = 0;
    virtual void Shutdown() = 0;
};

class IntelArcSyncClient final : public IIntelArcSyncClient
{
public:
    ~IntelArcSyncClient() override;
    bool Initialize() override;
    std::vector<IntelDisplayOutput> EnumerateDisplayOutputs() override;
    bool GetMonitorCapability(const IntelDisplayOutput&, IntelArcSyncCapability&) override;
    bool GetArcSyncProfile(const IntelDisplayOutput&, IntelArcSyncProfileState&) override;
    bool SetArcSyncProfile(const IntelDisplayOutput&, IntelArcSyncProfile, std::string& error) override;
    const std::vector<IntelArcSyncCall>& CallLog() const override { return calls_; }
    void Shutdown() override;

private:
    void Record(const char* operation, std::uint32_t result, std::string detail = {});
    void* library_{};
    void* apiHandle_{};
    std::vector<void*> adapters_;
    std::vector<IntelArcSyncCall> calls_;
};

const char* IntelArcSyncProfileName(IntelArcSyncProfile profile);
std::string IntelArcSyncResultName(std::uint32_t result);
}
