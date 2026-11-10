#include "AmdAdlxPowerTelemetry.h"
#include "AmdAdlxPowerTelemetryInternal.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
using namespace clawhud;
using namespace clawhud::detail;

void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct Fake
{
    int system{}, list{}, gpu{}, services{}, support{}, metrics{};
    AdlxPowerStatus init{AdlxPowerStatus::Ok}, listStatus{AdlxPowerStatus::Ok}, atStatus{AdlxPowerStatus::Ok};
    AdlxPowerStatus servicesStatus{AdlxPowerStatus::Ok}, supportStatus{AdlxPowerStatus::Ok};
    AdlxPowerStatus isSupportedStatus{AdlxPowerStatus::Ok};
    AdlxPowerStatus sampleStatus{AdlxPowerStatus::Ok}, powerStatus{AdlxPowerStatus::Ok};
    std::uint32_t count{1}, begin{7};
    bool powerSupported{true}, metricsOnFailure{}, nullSystem{}, nullList{}, nullServices{}, nullSupport{};
    bool owns{}, releaseFails{}, valid{true};
    std::vector<double> values{24.8};
    std::size_t next{};
    std::vector<std::uint32_t> selected;
    std::vector<void*> released;
    int initCalls{}, shutdowns{}, terminations{}, samples{}, powerReads{};

    static AdlxPowerStatus Init(void* c, void** p) noexcept
    {
        auto& f=*static_cast<Fake*>(c); ++f.initCalls; f.owns=f.init==AdlxPowerStatus::Ok;
        *p=f.owns&&!f.nullSystem?&f.system:nullptr; return f.init;
    }
    static AdlxPowerStatus List(void* c, void* s, void** p) noexcept
    {
        auto& f=*static_cast<Fake*>(c); f.valid &= s==&f.system;
        *p=f.listStatus==AdlxPowerStatus::Ok&&!f.nullList?&f.list:nullptr; return f.listStatus;
    }
    static AdlxPowerStatus Size(void* c, void* p, std::uint32_t* n) noexcept
    { auto& f=*static_cast<Fake*>(c); f.valid &= p==&f.list; *n=f.count; return AdlxPowerStatus::Ok; }
    static AdlxPowerStatus Begin(void* c, void* p, std::uint32_t* n) noexcept
    { auto& f=*static_cast<Fake*>(c); f.valid &= p==&f.list; *n=f.begin; return AdlxPowerStatus::Ok; }
    static AdlxPowerStatus At(void* c, void* p, std::uint32_t n, void** out) noexcept
    {
        auto& f=*static_cast<Fake*>(c); f.valid &= p==&f.list; f.selected.push_back(n);
        *out=f.atStatus==AdlxPowerStatus::Ok?&f.gpu:nullptr; return f.atStatus;
    }
    static AdlxPowerStatus Services(void* c, void* s, void** p) noexcept
    {
        auto& f=*static_cast<Fake*>(c); f.valid &= s==&f.system;
        *p=f.servicesStatus==AdlxPowerStatus::Ok&&!f.nullServices?&f.services:nullptr; return f.servicesStatus;
    }
    static AdlxPowerStatus MetricsSupport(void* c, void* s, void* g, void** p) noexcept
    {
        auto& f=*static_cast<Fake*>(c); f.valid &= s==&f.services && g==&f.gpu;
        *p=f.supportStatus==AdlxPowerStatus::Ok&&!f.nullSupport?&f.support:nullptr; return f.supportStatus;
    }
    static AdlxPowerStatus Supported(void* c, void* p, bool* out) noexcept
    { auto& f=*static_cast<Fake*>(c); f.valid &= p==&f.support; *out=f.powerSupported; return f.isSupportedStatus; }
    static AdlxPowerStatus Sample(void* c, void* s, void* g, void** p) noexcept
    {
        auto& f=*static_cast<Fake*>(c); f.valid &= s==&f.services && g==&f.gpu; ++f.samples;
        *p=f.sampleStatus==AdlxPowerStatus::Ok||f.metricsOnFailure?&f.metrics:nullptr; return f.sampleStatus;
    }
    static AdlxPowerStatus Power(void* c, void* p, double* out) noexcept
    {
        auto& f=*static_cast<Fake*>(c); f.valid &= p==&f.metrics; ++f.powerReads;
        if(f.powerStatus==AdlxPowerStatus::Ok&&f.next<f.values.size()) *out=f.values[f.next++];
        return f.powerStatus;
    }
    static bool Release(void* c, void* p) noexcept
    { auto& f=*static_cast<Fake*>(c); f.released.push_back(p); return !f.releaseFails; }
    static void Shutdown(void* c) noexcept
    { auto& f=*static_cast<Fake*>(c); ++f.shutdowns; if(f.owns) { ++f.terminations; f.owns=false; } }

    AdlxPowerApi Table() noexcept
    { return {this,Init,List,Size,Begin,At,Services,MetricsSupport,Supported,Sample,Power,Release,Shutdown}; }
};
void TestVersionAndInitializationFailures()
{
    Require(!SelectAdlxInitializationVersion(kAdlxMinimumApiVersion-1), "reject ADLX below 1.0");
    Require(SelectAdlxInitializationVersion(kAdlxMinimumApiVersion)==kAdlxMinimumApiVersion, "accept ADLX 1.0");
    Require(SelectAdlxInitializationVersion(kAdlxKnownHeaderVersion+1)==kAdlxKnownHeaderVersion, "cap API version");
    for (auto status : {AdlxPowerStatus::RuntimeUnavailable,AdlxPowerStatus::ExportUnavailable,AdlxPowerStatus::InitializationFailed}) {
        Fake f; f.init=status; AmdAdlxPowerTelemetry reader(f.Table());
        Require(reader.Initialize()==status && f.shutdowns==1 && f.terminations==0, "failed init must not terminate another owner");
    }
}
void TestGpuSelectionAndUnsupportedPaths()
{
    for (bool empty : {true,false}) {
        Fake f; f.count=empty?0:1; f.begin=12;
        if (!empty) f.atStatus=AdlxPowerStatus::NoGpu;
        AmdAdlxPowerTelemetry reader(f.Table());
        Require(reader.Initialize()==AdlxPowerStatus::NoGpu, "empty list or failed first item is unavailable");
        Require(empty?f.selected.empty():(f.selected==std::vector<std::uint32_t>{12}), "use Begin and never index an empty list");
        Require(f.terminations==1, "partial initialization must terminate owned ADLX");
    }
    Fake f; f.powerSupported=false; AmdAdlxPowerTelemetry reader(f.Table());
    Require(reader.Initialize()==AdlxPowerStatus::MetricUnsupported && f.samples==0, "unsupported power must not be sampled");
    Require(f.valid, "interfaces must be passed to the expected calls");
}
void TestPartialInitializationCleanup()
{
    for (int stage=0;stage<6;++stage) {
        Fake f;
        if(stage==0) f.nullSystem=true;
        if(stage==1) f.nullList=true;
        if(stage==2) f.servicesStatus=AdlxPowerStatus::PerformanceUnavailable;
        if(stage==3) f.nullSupport=true;
        if(stage==4) f.isSupportedStatus=AdlxPowerStatus::PerformanceUnavailable;
        if(stage==5) f.supportStatus=AdlxPowerStatus::PerformanceUnavailable;
        AmdAdlxPowerTelemetry reader(f.Table());
        Require(reader.Initialize()!=AdlxPowerStatus::Ok && f.terminations==1, "partial init must clean up owned runtime");
    }
}
void TestSamplingAndConversion()
{
    Fake f; f.values={24.9,0.0,12.1}; AmdAdlxPowerTelemetry reader(f.Table());
    Require(reader.Initialize()==AdlxPowerStatus::Ok, "reader initializes");
    Require(reader.ReadPowerW().watts==24 && reader.ReadPowerW().watts==0 && reader.ReadPowerW().watts==12,
        "each read is fresh, truncates watts, and preserves valid zero");
    Require(f.samples==3 && f.powerReads==3 && f.valid, "fresh metrics and GPU are used each read");
    f.powerStatus=AdlxPowerStatus::SampleFailed;
    const auto stale=reader.ReadPowerW();
    Require(stale.status==AdlxPowerStatus::SampleFailed&&!stale.watts, "a failed later read must not reuse the previous value");
    for (double value : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),-0.1,
             static_cast<double>(std::numeric_limits<int>::max())+1.0}) {
        Fake bad; bad.values={value}; AmdAdlxPowerTelemetry r(bad.Table()); r.Initialize();
        const auto result=r.ReadPowerW();
        Require(result.status==AdlxPowerStatus::InvalidValue&&!result.watts, "reject invalid or unrepresentable watts");
    }
    Fake bad; bad.sampleStatus=AdlxPowerStatus::SampleFailed; bad.metricsOnFailure=true;
    AmdAdlxPowerTelemetry r(bad.Table()); r.Initialize();
    const auto failed=r.ReadPowerW();
    Require(failed.status==AdlxPowerStatus::SampleFailed&&!failed.watts && bad.powerReads==0 &&
        bad.released==std::vector<void*>{&bad.metrics}, "failed metrics acquisition must be released without reading power");
}
void TestFailedReleaseAndShutdown()
{
    Fake f; f.values={18.5,21.0}; AmdAdlxPowerTelemetry reader(f.Table());
    Require(reader.Initialize()==AdlxPowerStatus::Ok, "reader initializes");
    f.releaseFails=true;
    Require(reader.ReadPowerW().status==AdlxPowerStatus::SampleFailed && reader.ReadPowerW().status==AdlxPowerStatus::SampleFailed,
        "failed release leaves the sample owned and unavailable");
    Require(f.samples==1 && f.powerReads==1, "do not overwrite an unreleased metrics interface");
    f.releaseFails=false; reader.Shutdown(); reader.Shutdown();
    Require(f.terminations==1 && f.released.size()>=5, "double shutdown releases children and terminates once");
    const auto n=f.released.size();
    Require(f.released[n-4]==&f.support && f.released[n-3]==&f.services && f.released[n-2]==&f.gpu && f.released[n-1]==&f.list,
        "release interfaces in reverse acquisition order");
    Require(reader.Initialize()==AdlxPowerStatus::Ok && f.initCalls==2, "explicit reinitialization works");
    reader.Shutdown(); Require(f.terminations==2, "reinitialized runtime is terminated once");
}
}

int main()
{
    TestVersionAndInitializationFailures();
    TestGpuSelectionAndUnsupportedPaths();
    TestPartialInitializationCleanup();
    TestSamplingAndConversion();
    TestFailedReleaseAndShutdown();
}
