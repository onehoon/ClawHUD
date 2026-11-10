#include "AmdAdlxPowerTelemetry.h"
#include "AmdAdlxPowerTelemetryInternal.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace clawhud
{
namespace
{
using Result=std::int32_t; using Version=std::uint64_t; using UInt=std::uint32_t;
using Bool=std::uint8_t;
constexpr Result kOk=0;
constexpr std::size_t kGetGpus=1, kGetServices=9, kRelease=1, kSize=3, kBegin=5, kAt=11;
constexpr std::size_t kPowerSupport=8, kCurrentMetrics=18, kSupportedMetrics=21, kGpuPower=9;
static_assert(sizeof(void*)==8 && kGetGpus*8==0x08 && kGetServices*8==0x48 && kAt*8==0x58);
static_assert(kPowerSupport*8==0x40 && kCurrentMetrics*8==0x90 && kSupportedMetrics*8==0xA8 && kGpuPower*8==0x48);
using QueryFn=Result(__cdecl*)(Version*);
using InitFn=Result(__cdecl*)(Version,void**);
using Init2Fn=Result(__cdecl*)(Version,void**,void**);
using TerminateFn=Result(__cdecl*)();
using GetFn=Result(__stdcall*)(void*,void**);
using GetPairFn=Result(__stdcall*)(void*,void*,void**);
using SizeFn=UInt(__stdcall*)(void*);
using AtFn=Result(__stdcall*)(void*,UInt,void**);
using BoolFn=Result(__stdcall*)(void*,Bool*);
using PowerFn=Result(__stdcall*)(void*,double*);
using ReleaseFn=std::int32_t(__stdcall*)(void*);

template<class T> T Resolve(HMODULE module,const char* name) noexcept
{ return reinterpret_cast<T>(GetProcAddress(module,name)); }
template<class T> T Method(void* object,std::size_t slot) noexcept
{
    if(!object) return nullptr; auto** table=*reinterpret_cast<void***>(object);
    return table?reinterpret_cast<T>(table[slot]):nullptr;
}

struct Native
{
    HMODULE module{}; bool owns{}; QueryFn query{}; InitFn init{}; Init2Fn init2{}; TerminateFn terminate{};
};
AdlxPowerStatus InitializeNative(void* opaque,void** system) noexcept
{
    auto& n=*static_cast<Native*>(opaque); *system=nullptr;
    if(n.module||n.owns) return AdlxPowerStatus::InitializationFailed;
    n.module=LoadLibraryExW(L"amdadlx64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!n.module) return AdlxPowerStatus::RuntimeUnavailable;
    n.query=Resolve<QueryFn>(n.module,"ADLXQueryFullVersion");
    n.init2=Resolve<Init2Fn>(n.module,"ADLXInitialize2");
    n.init=Resolve<InitFn>(n.module,"ADLXInitialize");
    n.terminate=Resolve<TerminateFn>(n.module,"ADLXTerminate");
    if(!n.query||!n.terminate||(!n.init2&&!n.init)) return AdlxPowerStatus::ExportUnavailable;
    Version runtime{};
    if(n.query(&runtime)!=kOk) return AdlxPowerStatus::InitializationFailed;
    const auto version=detail::SelectAdlxInitializationVersion(runtime);
    if(!version) return AdlxPowerStatus::InitializationFailed;
    void* mapping{};
    const auto result=n.init2?n.init2(*version,system,&mapping):n.init(*version,system);
    if(result!=kOk) { *system=nullptr; return AdlxPowerStatus::InitializationFailed; }
    n.owns=true; return *system?AdlxPowerStatus::Ok:AdlxPowerStatus::InitializationFailed;
}
AdlxPowerStatus GetGpus(void*,void* s,void** out) noexcept
{
    *out=nullptr; auto f=Method<GetFn>(s,kGetGpus);
    return f&&f(s,out)==kOk&&*out?AdlxPowerStatus::Ok:AdlxPowerStatus::NoGpu;
}
AdlxPowerStatus ListSize(void*,void* p,UInt* out) noexcept
{ auto f=Method<SizeFn>(p,kSize); if(!f)return AdlxPowerStatus::NoGpu; *out=f(p); return AdlxPowerStatus::Ok; }
AdlxPowerStatus ListBegin(void*,void* p,UInt* out) noexcept
{ auto f=Method<SizeFn>(p,kBegin); if(!f)return AdlxPowerStatus::NoGpu; *out=f(p); return AdlxPowerStatus::Ok; }
AdlxPowerStatus GpuAt(void*,void* p,UInt i,void** out) noexcept
{
    *out=nullptr; auto f=Method<AtFn>(p,kAt);
    return f&&f(p,i,out)==kOk&&*out?AdlxPowerStatus::Ok:AdlxPowerStatus::NoGpu;
}
AdlxPowerStatus GetServices(void*,void* s,void** out) noexcept
{
    *out=nullptr; auto f=Method<GetFn>(s,kGetServices);
    return f&&f(s,out)==kOk&&*out?AdlxPowerStatus::Ok:AdlxPowerStatus::PerformanceUnavailable;
}
AdlxPowerStatus GetSupport(void*,void* s,void* g,void** out) noexcept
{
    *out=nullptr; auto f=Method<GetPairFn>(s,kSupportedMetrics);
    return f&&f(s,g,out)==kOk&&*out?AdlxPowerStatus::Ok:AdlxPowerStatus::PerformanceUnavailable;
}
AdlxPowerStatus IsPowerSupported(void*,void* p,bool* out) noexcept
{
    *out=false; Bool value{}; auto f=Method<BoolFn>(p,kPowerSupport);
    if(!f||f(p,&value)!=kOk)return AdlxPowerStatus::PerformanceUnavailable;
    *out=value!=0; return AdlxPowerStatus::Ok;
}
AdlxPowerStatus GetMetrics(void*,void* s,void* g,void** out) noexcept
{
    *out=nullptr; auto f=Method<GetPairFn>(s,kCurrentMetrics);
    return f&&f(s,g,out)==kOk&&*out?AdlxPowerStatus::Ok:AdlxPowerStatus::SampleFailed;
}
AdlxPowerStatus ReadPower(void*,void* p,double* out) noexcept
{ auto f=Method<PowerFn>(p,kGpuPower); return f&&f(p,out)==kOk?AdlxPowerStatus::Ok:AdlxPowerStatus::SampleFailed; }
bool Release(void*,void* p) noexcept
{ auto f=Method<ReleaseFn>(p,kRelease); if(!f)return false; (void)f(p); return true; }
void ShutdownNative(void* opaque) noexcept
{
    auto& n=*static_cast<Native*>(opaque); if(!n.module)return;
    if(n.owns) { if(!n.terminate||n.terminate()!=kOk)return; n.owns=false; }
    FreeLibrary(n.module); n={};
}
detail::AdlxPowerApi NativeApi(Native* n) noexcept
{
    return {n,InitializeNative,GetGpus,ListSize,ListBegin,GpuAt,GetServices,GetSupport,
        IsPowerSupported,GetMetrics,ReadPower,Release,ShutdownNative};
}
}
std::optional<std::uint64_t> detail::SelectAdlxInitializationVersion(std::uint64_t runtime) noexcept
{
    if(runtime<detail::kAdlxMinimumApiVersion)return std::nullopt;
    return std::min(runtime,detail::kAdlxKnownHeaderVersion);
}
struct AmdAdlxPowerTelemetry::Impl
{
    std::unique_ptr<Native> native; detail::AdlxPowerApi api{};
    void *system{},*list{},*gpu{},*services{},*support{},*metrics{}; bool initialized{};
    bool ReleaseOne(void*& p) noexcept
    { if(!p)return true; if(!api.release||!api.release(api.context,p))return false; p=nullptr; return true; }
    bool ReleaseAll() noexcept
    {
        bool ok=ReleaseOne(metrics); ok=ReleaseOne(support)&&ok; ok=ReleaseOne(services)&&ok;
        ok=ReleaseOne(gpu)&&ok; return ReleaseOne(list)&&ok;
    }
    bool HasInterfaces() const noexcept { return metrics||support||services||gpu||list; }
};
AmdAdlxPowerTelemetry::AmdAdlxPowerTelemetry():impl_(std::make_unique<Impl>())
{ impl_->native=std::make_unique<Native>(); impl_->api=NativeApi(impl_->native.get()); }
#if defined(CLAWHUD_ADLX_POWER_TELEMETRY_TESTS)
AmdAdlxPowerTelemetry::AmdAdlxPowerTelemetry(detail::AdlxPowerApi api):impl_(std::make_unique<Impl>())
{ impl_->api=api; }
#endif
AmdAdlxPowerTelemetry::~AmdAdlxPowerTelemetry(){Shutdown();}
AdlxPowerStatus AmdAdlxPowerTelemetry::Initialize() noexcept
{
    if(impl_->initialized)return AdlxPowerStatus::Ok;
    if(impl_->system||impl_->HasInterfaces())Shutdown();
    if(impl_->HasInterfaces()||!impl_->api.initialize||!impl_->api.shutdown)
        return AdlxPowerStatus::InitializationFailed;
    const auto fail=[this](AdlxPowerStatus s){Shutdown(); return s;};
    auto s=impl_->api.initialize(impl_->api.context,&impl_->system);
    if(s!=AdlxPowerStatus::Ok||!impl_->system)
        return fail(s==AdlxPowerStatus::Ok?AdlxPowerStatus::InitializationFailed:s);
    if(!impl_->api.getGpuList||
       impl_->api.getGpuList(impl_->api.context,impl_->system,&impl_->list)!=AdlxPowerStatus::Ok||
       !impl_->list||!impl_->api.getGpuListSize)return fail(AdlxPowerStatus::NoGpu);
    UInt count{};
    if(impl_->api.getGpuListSize(impl_->api.context,impl_->list,&count)!=AdlxPowerStatus::Ok||!count||
       !impl_->api.getGpuListBegin||!impl_->api.getGpuAt)return fail(AdlxPowerStatus::NoGpu);
    UInt first{};
    if(impl_->api.getGpuListBegin(impl_->api.context,impl_->list,&first)!=AdlxPowerStatus::Ok||
       impl_->api.getGpuAt(impl_->api.context,impl_->list,first,&impl_->gpu)!=AdlxPowerStatus::Ok||!impl_->gpu)
        return fail(AdlxPowerStatus::NoGpu);
    if(!impl_->api.getPerformanceServices||
       impl_->api.getPerformanceServices(impl_->api.context,impl_->system,&impl_->services)!=AdlxPowerStatus::Ok||
       !impl_->services||!impl_->api.getSupportedGpuMetrics||
       impl_->api.getSupportedGpuMetrics(impl_->api.context,impl_->services,impl_->gpu,&impl_->support)!=AdlxPowerStatus::Ok||
       !impl_->support||!impl_->api.isGpuPowerSupported)return fail(AdlxPowerStatus::PerformanceUnavailable);
    bool supported{}; s=impl_->api.isGpuPowerSupported(impl_->api.context,impl_->support,&supported);
    if(s!=AdlxPowerStatus::Ok)return fail(AdlxPowerStatus::PerformanceUnavailable);
    if(!supported)return fail(AdlxPowerStatus::MetricUnsupported);
    impl_->initialized=true; return AdlxPowerStatus::Ok;
}
AdlxPowerReadResult AmdAdlxPowerTelemetry::ReadPowerW() noexcept
{
    if(!impl_->initialized||!impl_->api.getCurrentGpuMetrics||!impl_->api.readGpuPower)
        return {std::nullopt,AdlxPowerStatus::InitializationFailed};
    if(impl_->metrics&&!impl_->ReleaseOne(impl_->metrics))return {std::nullopt,AdlxPowerStatus::SampleFailed};
    const auto status=impl_->api.getCurrentGpuMetrics(impl_->api.context,impl_->services,impl_->gpu,&impl_->metrics);
    if(status!=AdlxPowerStatus::Ok||!impl_->metrics) {
        (void)impl_->ReleaseOne(impl_->metrics);
        return {std::nullopt,AdlxPowerStatus::SampleFailed};
    }
    double watts{}; const auto read=impl_->api.readGpuPower(impl_->api.context,impl_->metrics,&watts);
    const bool released=impl_->ReleaseOne(impl_->metrics);
    if(read!=AdlxPowerStatus::Ok||!released)return {std::nullopt,AdlxPowerStatus::SampleFailed};
    if(!std::isfinite(watts)||watts<0.0||watts>=static_cast<double>(std::numeric_limits<int>::max())+1.0)
        return {std::nullopt,AdlxPowerStatus::InvalidValue};
    return {static_cast<int>(watts),AdlxPowerStatus::Ok};
}
void AmdAdlxPowerTelemetry::Shutdown() noexcept
{
    if(!impl_)return;
    const bool released=impl_->ReleaseAll(); impl_->initialized=false;
    if(!released||impl_->HasInterfaces())return;
    if(impl_->api.shutdown)impl_->api.shutdown(impl_->api.context);
    impl_->system=nullptr;
}
}
