#include "IgclTelemetryDiagnostic.h"

#include "RuntimeLogger.h"
#include "Version.h"
#include "VrrDiagnostic.h"
#include "IgclApiCompat.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <thread>

namespace clawhud
{
namespace
{
using Result = std::uint32_t;
using Api = void*; using Device = void*; using Handle = void*;
using InitFn = Result(__cdecl*)(void*, Api*);
using CloseFn = Result(__cdecl*)(Api);
using EnumDevicesFn = Result(__cdecl*)(Api, std::uint32_t*, Device*);
using DevicePropsFn = Result(__cdecl*)(Device, void*);
using TelemetryFn = Result(__cdecl*)(Device, void*);
using EnumFn = Result(__cdecl*)(Device, std::uint32_t*, Handle*);
using OnePropsFn = Result(__cdecl*)(Handle, void*);
using OneStateFn = Result(__cdecl*)(Handle, void*);
using PciFn = Result(__cdecl*)(Device, void*);
using EnumOutputsFn = Result(__cdecl*)(Device, std::uint32_t*, Handle*);
using GetSet3DFn = Result(__cdecl*)(Device, void*);
using FanStateFn = Result(__cdecl*)(Handle, std::uint32_t, std::int32_t*);

constexpr Result kSuccess = 0;
constexpr std::uint32_t kUseLevelZero = 1;
constexpr std::uint32_t kApiVersion = (1u << 16) | 1u;
constexpr std::uint32_t kLiveState = 19;
constexpr std::uint32_t kCustom = 5;

using InitArgs = igcl::InitArgs; using LiveState = igcl::LiveState; using FeatureRequest = igcl::FeatureRequest;
using Item = igcl::Item; using PowerV2 = igcl::PowerTelemetryV2;
using DeviceProps = igcl::DeviceProperties; using PciProps = igcl::PciProperties;

template<class T> T Get(HMODULE m, const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
std::string ResultName(Result r) { switch(r) { case 0:return "CTL_RESULT_SUCCESS"; case 0x40000003:return "CTL_RESULT_ERROR_DEVICE_LOST"; case 0x40000006:return "CTL_RESULT_ERROR_INSUFFICIENT_PERMISSIONS"; case 0x40000007:return "CTL_RESULT_ERROR_NOT_AVAILABLE"; case 0x40000009:return "CTL_RESULT_ERROR_UNSUPPORTED_VERSION"; case 0x4000000a:return "CTL_RESULT_ERROR_UNSUPPORTED_FEATURE"; case 0x4000000b:return "CTL_RESULT_ERROR_INVALID_ARGUMENT"; case 0x4000000f:return "CTL_RESULT_ERROR_INVALID_SIZE"; case 0x40000010:return "CTL_RESULT_ERROR_UNSUPPORTED_SIZE"; case 0x40000015:return "CTL_RESULT_ERROR_NOT_IMPLEMENTED"; case 0x40000019:return "CTL_RESULT_ERROR_ZE_LOADER"; case 0x40000027:return "CTL_RESULT_ERROR_DEVICE_UNAVAILABLE"; default:return "UNKNOWN"; } }
std::string Hex(Result r) { std::ostringstream s; s<<"0x"<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill('0')<<r; return s.str(); }
std::wstring Now(bool file=false) { auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm tm{}; localtime_s(&tm,&t); std::wstringstream s; s<<std::put_time(&tm,file?L"%Y%m%d-%H%M%S":L"%Y-%m-%d %H:%M:%S"); return s.str(); }
std::string Narrow(const std::wstring& v) { if(v.empty())return{}; int n=WideCharToMultiByte(CP_UTF8,0,v.data(),(int)v.size(),nullptr,0,nullptr,nullptr); std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,v.data(),(int)v.size(),s.data(),n,nullptr,nullptr); return s; }
std::optional<std::string> ToIgclApplicationName(const std::wstring& filename)
{
    if(filename.empty())return std::nullopt;
    const int required=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,filename.data(),static_cast<int>(filename.size()),nullptr,0,nullptr,nullptr);
    if(required<=0||!IsIgclApplicationNameLengthValid(static_cast<std::size_t>(required)))return std::nullopt;
    std::string result(static_cast<std::size_t>(required),'\0');
    if(!WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,filename.data(),static_cast<int>(filename.size()),result.data(),required,nullptr,nullptr))return std::nullopt;
    return result;
}
bool Elevated() { HANDLE token{}; TOKEN_ELEVATION e{}; DWORD n{}; bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=FALSE && GetTokenInformation(token,TokenElevation,&e,sizeof(e),&n)!=FALSE; if(token)CloseHandle(token); return ok&&e.TokenIsElevated!=FALSE; }
void LogCall(std::ofstream& o,const char* n,Result r) { o<<n<<": "<<ResultName(r)<<" ("<<Hex(r)<<")\n"; }
std::string Class(Result r, bool supported=true) { if(!supported)return "UNSUPPORTED"; return r==0?"SUPPORTED":"API_ERROR"; }
template<class F> void Enumerate(std::ofstream& o,const char* name,Device d,F fn) { auto e=Get<EnumFn>(nullptr,name); (void)e; fn(); }
void ProbeCategory(std::ofstream& o, HMODULE lib, Device device, const char* title,
    const char* enumName, const char* propsName, const char* stateName)
{
    o << "\n=== " << title << " ===\n";
    const auto enumerate=Get<EnumFn>(lib,enumName);
    if(!enumerate){o<<enumName<<": SYMBOL_MISSING\n";return;}
    std::uint32_t count{}; Result r=enumerate(device,&count,nullptr); LogCall(o,enumName,r);
    if(r!=kSuccess || !count){o<<"Classification: "<<(r==kSuccess?"NO_DOMAIN":"API_ERROR")<<"\n";return;}
    std::vector<Handle> handles(count); r=enumerate(device,&count,handles.data()); LogCall(o,(std::string(enumName)+"(handles)").c_str(),r); if(r!=kSuccess)return;
    for(std::uint32_t i=0;i<count;++i)
    {
        o << "Object " << i << "\n";
        if (std::string(title) == "FREQUENCY") { igcl::FrequencyProperties p{}; p.Size=sizeof(p); auto f=Get<igcl::PropsFn>(lib,propsName); if(!f){o<<"Properties: SYMBOL_MISSING\n";continue;} r=f(handles[i],&p); LogCall(o,propsName,r); if(r==0)o<<"DomainType: "<<p.type<<"\nCanControl: "<<(p.canControl?"true":"false")<<"\nMinMHz: "<<p.min<<"\nMaxMHz: "<<p.max<<"\n"; igcl::FrequencyState s{};s.Size=sizeof(s);auto g=Get<igcl::StateFn>(lib,stateName);if(g){r=g(handles[i],&s);LogCall(o,stateName,r);if(r==0)o<<"CurrentVoltage: "<<s.currentVoltage<<"\nRequestMHz: "<<s.request<<"\nActualMHz: "<<s.actual<<"\nThrottleRaw: "<<s.throttleReasons<<"\n";} }
        else if (std::string(title) == "ENGINE") { igcl::EngineProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);o<<"EngineType: "<<p.type<<"\n";}igcl::EngineStats s{};s.Size=sizeof(s);auto g=Get<igcl::StateFn>(lib,stateName);if(g){r=g(handles[i],&s);LogCall(o,stateName,r);if(r==0)o<<"ActiveTimeUs: "<<s.activeTime<<"\nTimestampUs: "<<s.timestamp<<"\n";} }
        else if (std::string(title) == "MEMORY") { igcl::MemoryProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);if(r==0)o<<"MemoryType: "<<p.type<<"\nLocation: "<<p.location<<"\nPhysicalBytes: "<<p.physicalSize<<"\nBusWidth: "<<p.busWidth<<"\nChannels: "<<p.numChannels<<"\n";}igcl::MemoryState s{};s.Size=sizeof(s);auto g=Get<igcl::StateFn>(lib,stateName);if(g){s.Version=igcl::kLegacyStateVersion;r=g(handles[i],&s);LogCall(o,stateName,r);if(r==0)o<<"FreeBytes: "<<s.free<<"\nTotalBytes: "<<s.size<<"\n";}if(auto b=Get<igcl::StateFn>(lib,"ctlMemoryGetBandwidth")){igcl::MemoryBandwidth bw{};bw.Size=sizeof(bw);bw.Version=igcl::kTelemetryVersion;r=b(handles[i],&bw);LogCall(o,"ctlMemoryGetBandwidth",r);if(r==0)o<<"MaxBandwidth: "<<bw.maxBandwidth<<"\nReadCounter: "<<bw.readCounter<<"\nWriteCounter: "<<bw.writeCounter<<"\nTimestampUs: "<<bw.timestamp<<"\n";} }
        else if (std::string(title) == "TEMPERATURE") { igcl::TemperatureProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);o<<"SensorType: "<<p.type<<"\nMaxC: "<<p.maxTemperature<<"\n";}auto g=reinterpret_cast<Result(__cdecl*)(void*,double*)>(GetProcAddress(lib,stateName));if(g){double temperature=0;r=g(handles[i],&temperature);LogCall(o,stateName,r);if(r==0)o<<"CurrentC: "<<temperature<<"\n";} }
        else if (std::string(title) == "POWER") { igcl::PowerProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);o<<"CanControl: "<<(p.canControl?"true":"false")<<"\nDefaultLimitMilliwatts: "<<p.defaultLimit<<"\nMinLimitMilliwatts: "<<p.minLimit<<"\nMaxLimitMilliwatts: "<<p.maxLimit<<"\n";}igcl::PowerEnergy e{};e.Size=sizeof(e);auto g=Get<igcl::StateFn>(lib,stateName);if(g){r=g(handles[i],&e);LogCall(o,stateName,r);if(r==0)o<<"EnergyMicrojoules: "<<e.energy<<"\nTimestampUs: "<<e.timestamp<<"\n";} }
        else if (std::string(title) == "FAN") { igcl::FanProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);o<<"CanControl: "<<(p.canControl?"true":"false")<<"\nSupportedModes: "<<p.supportedModes<<"\nSupportedUnits: "<<p.supportedUnits<<"\nMaxRPM: "<<p.maxRPM<<"\n";}auto g=Get<igcl::FanStateFn>(lib,"ctlFanGetState");if(g){std::int32_t speed=-1;r=g(handles[i],0,&speed);LogCall(o,"ctlFanGetState",r);o<<"SpeedRaw: "<<speed<<"\n";} }
        else o << "Read-only output handle enumerated; typed display properties are unavailable in this pinned subset.\n";
    }
}
double DecodeItem(const Item& item) noexcept
{
    switch (item.type) { case 0:return item.value.i8; case 1:return item.value.u8; case 2:return item.value.i16; case 3:return item.value.u16; case 4:return item.value.i32; case 5:return item.value.u32; case 6:return static_cast<double>(item.value.i64); case 7:return static_cast<double>(item.value.u64); case 8:return item.value.f; case 9:return item.value.d; default:return 0.0; }
}
void SampleDynamicDomains(HMODULE lib, Device device, std::size_t adapter,
    std::map<std::string, IgclSampleSeries>& metrics)
{
    auto key=[&](const std::string& name){return "adapter["+std::to_string(adapter)+"]."+name;};
    auto fail=[&](const std::string& name,bool symbol,bool domain,bool api){auto& s=metrics[key(name)];s.symbolPresent&=symbol;s.hasDomain&=domain;s.apiSucceeded&=api;};
    auto collect=[&](const std::string& name,double value,std::uint64_t raw,std::uint32_t type,bool ok){auto& s=metrics[key(name)];const auto separator=name.rfind('.');if(separator!=std::string::npos){const auto& parent=metrics[key(name.substr(0,separator))];s.symbolPresent&=parent.symbolPresent;s.hasDomain&=parent.hasDomain;s.apiSucceeded&=parent.apiSucceeded;}s.apiSucceeded&=ok;if(ok){s.values.push_back(value);s.rawValues.push_back(raw);s.types.push_back(type);}};
    auto sample=[&](const char* ename,const char* sname,const char* metric){auto en=Get<EnumFn>(lib,ename);auto st=Get<OneStateFn>(lib,sname);if(!en||!st){fail(metric,en&&st,true,true);return;}std::uint32_t n{};const Result cr=en(device,&n,nullptr);if(cr!=kSuccess){fail(metric,true,true,false);return;}if(!n){fail(metric,true,false,true);return;}std::vector<Handle> hs(n);const Result er=en(device,&n,hs.data());if(er!=kSuccess){fail(metric,true,true,false);return;}for(std::uint32_t i=0;i<n;++i){const std::string index=std::string(metric)+"["+std::to_string(i)+"]";if(std::string(metric)=="frequency"){igcl::FrequencyState v{};v.Size=sizeof(v);const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess)collect(index+".actual",v.actual,static_cast<std::uint64_t>(v.actual),9,true);}else if(std::string(metric)=="engine"){igcl::EngineStats v{};v.Size=sizeof(v);const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess)collect(index+".activeTime",static_cast<double>(v.activeTime),v.activeTime,7,true);}else if(std::string(metric)=="memory"){igcl::MemoryState v{};v.Size=sizeof(v);const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess)collect(index+".free",static_cast<double>(v.free),v.free,7,true);auto bw=Get<OneStateFn>(lib,"ctlMemoryGetBandwidth");auto& bs=metrics[key(index+".bandwidth.readCounter")];if(!bw){bs.symbolPresent=false;}else{igcl::MemoryBandwidth b{};b.Size=sizeof(b);const Result br=bw(hs[i],&b);bs.apiSucceeded&=br==kSuccess;if(br==kSuccess){collect(index+".bandwidth.readCounter",static_cast<double>(b.readCounter),b.readCounter,7,true);collect(index+".bandwidth.writeCounter",static_cast<double>(b.writeCounter),b.writeCounter,7,true);}}}else if(std::string(metric)=="power"){igcl::PowerEnergy v{};v.Size=sizeof(v);const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess)collect(index+".energy",static_cast<double>(v.energy),v.energy,7,true);}}};
    auto sampleFull=[&](const char* ename,const char* sname,const char* metric){
        auto en=Get<EnumFn>(lib,ename); auto st=Get<OneStateFn>(lib,sname);
        if(!en||!st){fail(metric,en&&st,true,true);return;}
        std::uint32_t n{}; const Result cr=en(device,&n,nullptr);
        if(cr!=kSuccess){fail(metric,true,true,false);return;} if(!n){fail(metric,true,false,true);return;}
        std::vector<Handle> hs(n); const Result er=en(device,&n,hs.data()); if(er!=kSuccess){fail(metric,true,true,false);return;}
        for(std::uint32_t i=0;i<n;++i){const std::string index=std::string(metric)+"["+std::to_string(i)+"]";
            if(std::string(metric)=="frequency"){igcl::FrequencyState v{};v.Size=sizeof(v);v.Version=igcl::kLegacyStateVersion;const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess){collect(index+".currentVoltage",v.currentVoltage,0,9,true);collect(index+".request",v.request,0,9,true);collect(index+".tdp",v.tdp,0,9,true);collect(index+".efficient",v.efficient,0,9,true);collect(index+".actual",v.actual,0,9,true);collect(index+".throttleReasons",static_cast<double>(v.throttleReasons),v.throttleReasons,7,true);}}
            else if(std::string(metric)=="engine"){igcl::EngineStats v{};v.Size=sizeof(v);v.Version=igcl::kLegacyStateVersion;const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess){collect(index+".activeTime",static_cast<double>(v.activeTime),v.activeTime,7,true);collect(index+".timestamp",static_cast<double>(v.timestamp),v.timestamp,7,true);}}
            else if(std::string(metric)=="memory"){igcl::MemoryState v{};v.Size=sizeof(v);v.Version=igcl::kLegacyStateVersion;const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess){collect(index+".free",static_cast<double>(v.free),v.free,7,true);collect(index+".size",static_cast<double>(v.size),v.size,7,true);}auto bw=Get<OneStateFn>(lib,"ctlMemoryGetBandwidth");if(!bw){fail(index+".bandwidth",false,true,true);}else{igcl::MemoryBandwidth b{};b.Size=sizeof(b);b.Version=igcl::kTelemetryVersion;const Result br=bw(hs[i],&b);fail(index+".bandwidth",true,true,br==kSuccess);if(br==kSuccess){collect(index+".bandwidth.maxBandwidth",static_cast<double>(b.maxBandwidth),b.maxBandwidth,7,true);collect(index+".bandwidth.timestamp",static_cast<double>(b.timestamp),b.timestamp,7,true);collect(index+".bandwidth.readCounter",static_cast<double>(b.readCounter),b.readCounter,7,true);collect(index+".bandwidth.writeCounter",static_cast<double>(b.writeCounter),b.writeCounter,7,true);}}}
            else if(std::string(metric)=="power"){igcl::PowerEnergy v{};v.Size=sizeof(v);v.Version=igcl::kLegacyStateVersion;const Result r=st(hs[i],&v);fail(index,true,true,r==kSuccess);if(r==kSuccess){collect(index+".energy",static_cast<double>(v.energy),v.energy,7,true);collect(index+".timestamp",static_cast<double>(v.timestamp),v.timestamp,7,true);}}
        }
    };
    sampleFull("ctlEnumFrequencyDomains","ctlFrequencyGetState","frequency"); sampleFull("ctlEnumEngineGroups","ctlEngineGetActivity","engine"); sampleFull("ctlEnumMemoryModules","ctlMemoryGetState","memory"); sampleFull("ctlEnumPowerDomains","ctlPowerGetEnergyCounter","power");
    auto temps=Get<EnumFn>(lib,"ctlEnumTemperatureSensors");auto tempState=Get<OneStateFn>(lib,"ctlTemperatureGetState");if(!temps||!tempState)fail("temperature",temps&&tempState,true,true);else{std::uint32_t n{};const Result r=temps(device,&n,nullptr);if(r!=kSuccess)fail("temperature",true,true,false);else if(!n)fail("temperature",true,false,true);else{std::vector<Handle> hs(n);if(temps(device,&n,hs.data())!=kSuccess)fail("temperature",true,true,false);else for(std::uint32_t i=0;i<n;++i){double value{};const Result tr=reinterpret_cast<Result(__cdecl*)(void*,double*)>(tempState)(hs[i],&value);fail("temperature["+std::to_string(i)+"].current",true,true,tr==kSuccess);if(tr==kSuccess)collect("temperature["+std::to_string(i)+"].current",value,static_cast<std::uint64_t>(value),9,true);}}}
    auto fans=Get<EnumFn>(lib,"ctlEnumFans");auto fanState=Get<FanStateFn>(lib,"ctlFanGetState");if(!fans||!fanState)fail("fan",fans&&fanState,true,true);else{std::uint32_t n{};const Result r=fans(device,&n,nullptr);if(r!=kSuccess)fail("fan",true,true,false);else if(!n)fail("fan",true,false,true);else{std::vector<Handle> hs(n);if(fans(device,&n,hs.data())!=kSuccess)fail("fan",true,true,false);else for(std::uint32_t i=0;i<n;++i){std::int32_t speed=-1;const Result fr=fanState(hs[i],0,&speed);fail("fan["+std::to_string(i)+"].speed",true,true,fr==kSuccess);if(fr==kSuccess)collect("fan["+std::to_string(i)+"].speed",static_cast<double>(speed),static_cast<std::uint64_t>(speed),4,true);}}}
}
template<class ItemFn, class BoolFn>
void VisitPowerTelemetry(const PowerV2& t, ItemFn&& item, BoolFn&& boolean)
{
    item("timeStamp",t.timeStamp); item("gpuEnergyCounter",t.gpuEnergyCounter); item("gpuVoltage",t.gpuVoltage); item("gpuCurrentClockFrequency",t.gpuCurrentClockFrequency); item("gpuCurrentTemperature",t.gpuCurrentTemperature); item("globalActivityCounter",t.globalActivityCounter); item("renderComputeActivityCounter",t.renderComputeActivityCounter); item("mediaActivityCounter",t.mediaActivityCounter); item("vramEnergyCounter",t.vramEnergyCounter); item("vramVoltage",t.vramVoltage); item("vramCurrentClockFrequency",t.vramCurrentClockFrequency); item("vramCurrentEffectiveFrequency",t.vramCurrentEffectiveFrequency); item("vramReadBandwidthCounter",t.vramReadBandwidthCounter); item("vramWriteBandwidthCounter",t.vramWriteBandwidthCounter); item("vramCurrentTemperature",t.vramCurrentTemperature); item("totalCardEnergyCounter",t.totalCardEnergyCounter); item("gpuVrTemp",t.gpuVrTemp); item("vramVrTemp",t.vramVrTemp); item("saVrTemp",t.saVrTemp); item("gpuEffectiveClock",t.gpuEffectiveClock); item("gpuOverVoltagePercent",t.gpuOverVoltagePercent); item("gpuPowerPercent",t.gpuPowerPercent); item("gpuTemperaturePercent",t.gpuTemperaturePercent); item("vramReadBandwidth",t.vramReadBandwidth); item("vramWriteBandwidth",t.vramWriteBandwidth);
    boolean("gpuPowerLimited",t.gpuPowerLimited); boolean("gpuTemperatureLimited",t.gpuTemperatureLimited); boolean("gpuCurrentLimited",t.gpuCurrentLimited); boolean("gpuVoltageLimited",t.gpuVoltageLimited); boolean("gpuUtilizationLimited",t.gpuUtilizationLimited);
    for(std::size_t i=0;i<std::size(t.psu);++i){boolean("psu["+std::to_string(i)+"].supported",t.psu[i].supported);item("psu["+std::to_string(i)+"].energy",t.psu[i].energy);item("psu["+std::to_string(i)+"].voltage",t.psu[i].voltage);}
    for(std::size_t i=0;i<std::size(t.fanSpeed);++i)item("fanSpeed["+std::to_string(i)+"]",t.fanSpeed[i]);
}
}

const char* IgclDiagnosticClassName(IgclDiagnosticClass v) noexcept { switch(v){case IgclDiagnosticClass::SupportedActive:return "SUPPORTED_ACTIVE";case IgclDiagnosticClass::SupportedZero:return "SUPPORTED_ZERO";case IgclDiagnosticClass::SupportedConstant:return "SUPPORTED_CONSTANT";case IgclDiagnosticClass::Unsupported:return "UNSUPPORTED";case IgclDiagnosticClass::NoDomain:return "NO_DOMAIN";case IgclDiagnosticClass::SymbolMissing:return "SYMBOL_MISSING";case IgclDiagnosticClass::ApiError:return "API_ERROR";default:return "SKIPPED_MUTATION_CAPABLE";} }
IgclDiagnosticClass ClassifyIgclSamples(const IgclSampleSeries& s) noexcept { if(!s.symbolPresent)return IgclDiagnosticClass::SymbolMissing; if(!s.hasDomain)return IgclDiagnosticClass::NoDomain; if(!s.apiSucceeded)return IgclDiagnosticClass::ApiError; if(!s.supported)return IgclDiagnosticClass::Unsupported; if(s.values.empty())return IgclDiagnosticClass::SupportedZero; const bool integerType=!s.types.empty()&&std::all_of(s.types.begin(),s.types.end(),[](std::uint32_t t){return t<=7;}); if(integerType&&!s.rawValues.empty()){const auto [a,b]=std::minmax_element(s.rawValues.begin(),s.rawValues.end());if(*a==0&&*b==0)return IgclDiagnosticClass::SupportedZero;return *a==*b?IgclDiagnosticClass::SupportedConstant:IgclDiagnosticClass::SupportedActive;} const auto [a,b]=std::minmax_element(s.values.begin(),s.values.end()); if(*a==0.0&&*b==0.0)return IgclDiagnosticClass::SupportedZero; return *a==*b?IgclDiagnosticClass::SupportedConstant:IgclDiagnosticClass::SupportedActive; }
double IgclSampleMinimum(const IgclSampleSeries& s) noexcept { return s.values.empty()?0.0:*std::min_element(s.values.begin(),s.values.end()); }
double IgclSampleMaximum(const IgclSampleSeries& s) noexcept { return s.values.empty()?0.0:*std::max_element(s.values.begin(),s.values.end()); }
bool IsIgclApplicationNameLengthValid(std::size_t length) noexcept { return length <= static_cast<std::size_t>(INT8_MAX); }

class IgclTelemetryDiagnostic::Impl {};
IgclTelemetryDiagnostic::~IgclTelemetryDiagnostic(){ Stop(); }
bool IgclTelemetryDiagnostic::Start(){ if(running_.exchange(true))return false; stop_=false; try{worker_=new std::thread(&IgclTelemetryDiagnostic::Run,this);}catch(...){running_=false;delete worker_;worker_=nullptr;throw;} Status(L"Waiting 5 seconds..."); return true; }
void IgclTelemetryDiagnostic::Stop() noexcept { stop_=true; if(worker_&&worker_->joinable())worker_->join(); delete worker_;worker_=nullptr;running_=false; }
void IgclTelemetryDiagnostic::Status(const wchar_t* t) const { if(!notifyWindow_)return; auto* s=new std::wstring(t); if(!PostMessageW(notifyWindow_,kIgclDiagnosticStatus,(WPARAM)s,0))delete s; }

void IgclTelemetryDiagnostic::Run()
{
    bool success=false; std::ofstream log; HMODULE library=nullptr; Api api=nullptr; std::map<std::string, IgclSampleSeries> metrics;
    try {
        std::this_thread::sleep_for(std::chrono::seconds(5)); if(stop_) { Status(L"Cancelled"); throw std::runtime_error("cancelled"); }
        HWND fg=GetForegroundWindow(); DWORD pid=0; GetWindowThreadProcessId(fg,&pid); std::wstring path;
        if(pid){HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(p){wchar_t b[32768]{};DWORD n=32768;if(QueryFullProcessImageNameW(p,0,b,&n))path.assign(b,n);CloseHandle(p);}}
        auto out=LogDirectory()/(L"igcl-"+Now(true)+L".txt"); log.open(out,std::ios::binary); if(!log.is_open())throw std::runtime_error("log");
        log<<"ClawHUD IGCL Diagnostic\n=== ENVIRONMENT ===\nWall Time: "<<Narrow(Now())<<"\nClawHUD Version: "<<Narrow(CLAWHUD_VERSION)<<"\nProcess Elevation: "<<(Elevated()?"YES":"NO")<<"\nForeground HWND: 0x"<<std::hex<<(std::uintptr_t)fg<<"\nForeground PID: "<<std::dec<<pid<<"\nForeground Path: "<<Narrow(path)<<"\n";
        library=LoadLibraryExW(L"ControlLib.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32); log<<"ControlLib.dll: "<<(library?"LOADED":"LOAD_FAILED")<<" (System32 search)\n"; if(!library){log<<"Classification: SYMBOL_MISSING\n";throw std::runtime_error("load");}
        const auto init=Get<InitFn>(library,"ctlInit"); const auto close=Get<CloseFn>(library,"ctlClose"); log<<"ctlInit symbol: "<<(init?"PRESENT":"MISSING")<<"\n"; if(!init||!close)throw std::runtime_error("symbol");
        InitArgs args{};args.size=sizeof(args);args.appVersion=kApiVersion;args.flags=kUseLevelZero;Result r=init(&args,&api);log<<"IGCL initialization result: "<<ResultName(r)<<" ("<<Hex(r)<<")\nRequested API version: 1.1\nSupported/returned API version: "<<(args.supportedVersion>>16)<<"."<<(args.supportedVersion&0xffff)<<"\nInitialization flags: CTL_INIT_FLAG_USE_LEVEL_ZERO (0x1)\n";if(r!=kSuccess)throw std::runtime_error("init");
        auto en=Get<EnumDevicesFn>(library,"ctlEnumerateDevices"); if(!en){log<<"ctlEnumerateDevices: SYMBOL_MISSING\n";throw std::runtime_error("enum symbol");} std::uint32_t count=0;r=en(api,&count,nullptr);LogCall(log,"ctlEnumerateDevices(count)",r);if(r!=kSuccess) { log<<"ctlEnumerateDevices(count): API_ERROR\n"; throw std::runtime_error("enum count"); } if(!count){log<<"Adapters: NO_DOMAIN\n";success=true;throw std::runtime_error("no adapters");}std::vector<Device> ds(count);r=en(api,&count,ds.data());LogCall(log,"ctlEnumerateDevices(handles)",r);if(r!=kSuccess)throw std::runtime_error("enum");
        auto devProps=Get<DevicePropsFn>(library,"ctlGetDeviceProperties");auto pci=Get<PciFn>(library,"ctlPciGetProperties");auto pciState=Get<PciFn>(library,"ctlPciGetState");auto tele=Get<TelemetryFn>(library,"ctlPowerTelemetryGetV2");
        for(std::uint32_t i=0;i<count;++i){log<<"\n=== ADAPTER "<<i<<" ===\n";if(devProps){DeviceProps p{};p.size=sizeof(p);r=devProps(ds[i],&p);LogCall(log,"ctlGetDeviceProperties",r);if(r==0)log<<"Device: "<<p.name<<"\nVendor ID: 0x"<<std::hex<<p.vendor<<" Device ID: 0x"<<p.device<<" Revision: 0x"<<p.revision<<"\nDevice Type: "<<std::dec<<p.deviceType<<"\nDriver Version Raw: "<<p.driverVersion<<"\nGraphics Capability Flags Raw: 0x"<<std::hex<<p.graphicsFlags<<"\n";}else log<<"ctlGetDeviceProperties: SYMBOL_MISSING\n";if(pci){PciProps p{};p.size=sizeof(p);r=pci(ds[i],&p);LogCall(log,"ctlPciGetProperties",r);log<<"PCI BDF: "<<p.address.domain<<":"<<p.address.bus<<":"<<p.address.device<<":"<<p.address.function<<"\nPCI max gen: "<<p.maxSpeed.gen<<" width: "<<p.maxSpeed.width<<" max bandwidth: "<<p.maxSpeed.maxBandwidth<<"\nResizable BAR supported: "<<(p.resizable_bar_supported?"true":"false")<<" enabled: "<<(p.resizable_bar_enabled?"true":"false")<<"\n";}else log<<"ctlPciGetProperties: SYMBOL_MISSING\n";log<<"\n=== POWER TELEMETRY V2 ===\n";if(tele){PowerV2 p{};p.size=sizeof(p);p.version=igcl::kTelemetryVersion;r=tele(ds[i],&p);LogCall(log,"ctlPowerTelemetryGetV2",r);const char* names[]={"timeStamp","gpuEnergyCounter","gpuVoltage","gpuCurrentClockFrequency","gpuCurrentTemperature","globalActivityCounter","renderComputeActivityCounter","mediaActivityCounter","vramEnergyCounter","vramVoltage","vramCurrentClockFrequency","vramCurrentEffectiveFrequency","vramReadBandwidthCounter","vramWriteBandwidthCounter","vramCurrentTemperature","totalCardEnergyCounter","gpuVrTemp","vramVrTemp","saVrTemp","gpuEffectiveClock","gpuOverVoltagePercent","gpuPowerPercent","gpuTemperaturePercent","vramReadBandwidth","vramWriteBandwidth"};Item* items[]={&p.timeStamp,&p.gpuEnergyCounter,&p.gpuVoltage,&p.gpuCurrentClockFrequency,&p.gpuCurrentTemperature,&p.globalActivityCounter,&p.renderComputeActivityCounter,&p.mediaActivityCounter,&p.vramEnergyCounter,&p.vramVoltage,&p.vramCurrentClockFrequency,&p.vramCurrentEffectiveFrequency,&p.vramReadBandwidthCounter,&p.vramWriteBandwidthCounter,&p.vramCurrentTemperature,&p.totalCardEnergyCounter,&p.gpuVrTemp,&p.vramVrTemp,&p.saVrTemp,&p.gpuEffectiveClock,&p.gpuOverVoltagePercent,&p.gpuPowerPercent,&p.gpuTemperaturePercent,&p.vramReadBandwidth,&p.vramWriteBandwidth};for(size_t j=0;j<std::size(names);++j)log<<names[j]<<"\n  Supported: "<<(items[j]->supported?"true":"false")<<"\n  Type: "<<(unsigned)items[j]->type<<"\n  Units: "<<items[j]->units<<"\n  Raw uint64: "<<items[j]->value.u64<<"\n  Raw double: "<<items[j]->value.d<<"\n";}else log<<"ctlPowerTelemetryGetV2: SYMBOL_MISSING\n";
            log<<"\n=== SAFE SURFACE SYMBOL INVENTORY ===\n";const char* syms[]={"ctlEnumFrequencyDomains","ctlFrequencyGetProperties","ctlFrequencyGetState","ctlEnumEngineGroups","ctlEngineGetProperties","ctlEngineGetActivity","ctlEnumMemoryModules","ctlMemoryGetProperties","ctlMemoryGetState","ctlMemoryGetBandwidth","ctlEnumTemperatureSensors","ctlTemperatureGetProperties","ctlTemperatureGetState","ctlEnumPowerDomains","ctlPowerGetProperties","ctlPowerGetEnergyCounter","ctlEnumFans","ctlFanGetProperties","ctlFanGetState","ctlEnumerateDisplayOutputs","ctlGetDisplayProperties","ctlGetSet3DFeature"};for(auto s:syms)log<<s<<": "<<(Get<void*>(library,s)?"PRESENT_READ_ONLY_OR_MIXED":"SYMBOL_MISSING")<<"\n";log<<"Mutation-capable setters are never dispatched: SKIPPED_MUTATION_CAPABLE\n";}
        for(std::uint32_t i=0;i<count;++i)
        {
            log << "\n=== ADAPTER " << i << " READ-ONLY DOMAINS ===\n";
            if (pciState)
            {
                igcl::PciState state{}; state.Size=sizeof(state); const Result pr=pciState(ds[i],&state);
                LogCall(log,"ctlPciGetState",pr); if(pr==kSuccess) log<<"PCI current generation: "<<state.speed.gen<<" width: "<<state.speed.width<<" max bandwidth: "<<state.speed.maxBandwidth<<"\n";
            }
            log << "\n=== 3D LIVE STATE ===\n";
            if (const auto live = Get<GetSet3DFn>(library, "ctlGetSet3DFeature"); live)
            {
                LiveState state{}; FeatureRequest request{}; request.size=sizeof(request); request.featureType=19; request.valueType=5; request.customValueSize=sizeof(state); request.customValue=&state;
                std::optional<std::string> appName;
                if(!path.empty())
                {
                    try { appName=ToIgclApplicationName(std::filesystem::path(path).filename().wstring()); }
                    catch(const std::exception&) { appName=std::nullopt; }
                }
                if(!path.empty()&&!appName)
                {
                    log<<"ctlGetSet3DFeature (read-only): SKIPPED_INVALID_APPLICATION_NAME\n";
                }
                else
                {
                    request.applicationName=appName?appName->data():nullptr; request.applicationNameLength=static_cast<std::int8_t>(appName?appName->size():0);
                    const Result lr=live(ds[i],&request); LogCall(log,"ctlGetSet3DFeature (read-only)",lr); log<<"Graphics API mask raw: 0x"<<std::hex<<state.gfxApi<<"\nTarget FPS raw: "<<std::dec<<state.targetFps<<"\nFrame pacing raw: "<<state.framePacingStatus<<"\n";
                }
            }
            else log << "ctlGetSet3DFeature: SYMBOL_MISSING\n";
            ProbeCategory(log,library,ds[i],"FREQUENCY","ctlEnumFrequencyDomains","ctlFrequencyGetProperties","ctlFrequencyGetState");
            ProbeCategory(log,library,ds[i],"ENGINE","ctlEnumEngineGroups","ctlEngineGetProperties","ctlEngineGetActivity");
            ProbeCategory(log,library,ds[i],"MEMORY","ctlEnumMemoryModules","ctlMemoryGetProperties","ctlMemoryGetState");
            ProbeCategory(log,library,ds[i],"TEMPERATURE","ctlEnumTemperatureSensors","ctlTemperatureGetProperties","ctlTemperatureGetState");
            ProbeCategory(log,library,ds[i],"POWER","ctlEnumPowerDomains","ctlPowerGetProperties","ctlPowerGetEnergyCounter");
            ProbeCategory(log,library,ds[i],"FAN","ctlEnumFans","ctlFanGetProperties",nullptr);
            ProbeCategory(log,library,ds[i],"DISPLAY / OUTPUT","ctlEnumerateDisplayOutputs","ctlGetDisplayProperties",nullptr);
            if (auto outputs=Get<EnumOutputsFn>(library,"ctlEnumerateDisplayOutputs"))
            {
                std::uint32_t outputCount{}; Result orr=outputs(ds[i],&outputCount,nullptr); if(orr==kSuccess && outputCount){std::vector<Handle> handles(outputCount);orr=outputs(ds[i],&outputCount,handles.data());for(std::uint32_t oi=0;orr==kSuccess&&oi<outputCount;++oi){if(auto props=Get<OnePropsFn>(library,"ctlGetDisplayProperties")){igcl::DisplayProperties p{};p.Size=sizeof(p);p.Version=1;p.displayTiming.Size=sizeof(p.displayTiming);p.displayTiming.Version=1;const Result dr=props(handles[oi],&p);LogCall(log,"ctlGetDisplayProperties",dr);if(dr==kSuccess)log<<"Output "<<oi<<" WindowsEncoderID: "<<p.Os_display_encoder_handle.WindowsDisplayEncoderID<<" Type: "<<p.type<<" AttachedMux: "<<p.attachedMux<<" ProtocolOutput: "<<p.protocolConverterOutput<<" SupportedSpec: "<<(unsigned)p.supportedSpec.major_version<<"."<<(unsigned)p.supportedSpec.minor_version<<"."<<(unsigned)p.supportedSpec.revision_version<<" BPC flags: "<<p.supportedOutputBpcFlags<<" Config flags: "<<p.displayConfigFlags<<" AdvancedSupported: "<<p.advancedFeatureSupportedFlags<<" RefreshRate: "<<p.displayTiming.RefreshRate<<"\n";}else log<<"ctlGetDisplayProperties: SYMBOL_MISSING\n";}}
            }
        }
        log<<"\n=== SAMPLING ===\nCadence: 250 ms\nTarget samples: 20\nRaw samples retain support/type/unit/value fields above. Counters are not converted to instantaneous values.\n";
        const char* powerMetricNames[] = {
            "timeStamp","gpuEnergyCounter","gpuVoltage","gpuCurrentClockFrequency","gpuCurrentTemperature",
            "globalActivityCounter","renderComputeActivityCounter","mediaActivityCounter",
            "gpuPowerLimited","gpuTemperatureLimited","gpuCurrentLimited","gpuVoltageLimited","gpuUtilizationLimited",
            "vramEnergyCounter","vramVoltage","vramCurrentClockFrequency","vramCurrentEffectiveFrequency",
            "vramReadBandwidthCounter","vramWriteBandwidthCounter","vramCurrentTemperature","totalCardEnergyCounter",
            "gpuVrTemp","vramVrTemp","saVrTemp","gpuEffectiveClock","gpuOverVoltagePercent","gpuPowerPercent",
            "gpuTemperaturePercent","vramReadBandwidth","vramWriteBandwidth"
        };
        auto markPowerUnavailable = [&](std::uint32_t adapter, bool symbolPresent, bool apiSucceeded) {
            const auto prefix = "adapter["+std::to_string(adapter)+"].";
            for (const auto name : powerMetricNames) {
                auto& series = metrics[prefix+name];
                series.symbolPresent &= symbolPresent;
                series.apiSucceeded &= apiSucceeded;
            }
            for (std::size_t i=0; i<5; ++i) {
                for (const auto suffix : {"supported", "energy", "voltage"}) {
                    auto& series = metrics[prefix+"psu["+std::to_string(i)+"]."+suffix];
                    series.symbolPresent &= symbolPresent;
                    series.apiSucceeded &= apiSucceeded;
                }
                auto& fan = metrics[prefix+"fanSpeed["+std::to_string(i)+"]"];
                fan.symbolPresent &= symbolPresent;
                fan.apiSucceeded &= apiSucceeded;
            }
        };
        std::size_t currentAdapter=0;
        auto collect = [&](const std::string& name, const Item& item, bool counter=false) { auto& s=metrics["adapter["+std::to_string(currentAdapter)+"]."+name]; s.supported&=item.supported; if(item.supported){s.values.push_back(DecodeItem(item)); s.rawValues.push_back(item.value.u64); s.timestamps.push_back(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())); s.types.push_back(item.type);} (void)counter; };
        constexpr auto samplingCadence = std::chrono::milliseconds(250);
        const auto samplingStart = std::chrono::steady_clock::now();
        auto nextSample = samplingStart;
        for (int sample=1; sample<=20 && !stop_; ++sample)
        {
            const auto sampleTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - samplingStart).count();
            log << "Sample " << std::setw(2) << std::setfill('0') << sample << " timestamp_ms="
                << sampleTimestamp << "\n";
            for (std::uint32_t i=0;i<count;++i)
            {
                currentAdapter=i;
                SampleDynamicDomains(library, ds[i], i, metrics);
                PowerV2 p{}; p.size=sizeof(p); p.version=igcl::kTelemetryVersion; const Result sr=tele ? tele(ds[i],&p) : 0x40000000u;
                if (tele && sr == kSuccess)
                {
                    auto collectBool = [&](const std::string& name, bool value) { auto& s=metrics["adapter["+std::to_string(currentAdapter)+"]."+name]; s.values.push_back(value?1.0:0.0); s.rawValues.push_back(value?1:0); s.types.push_back(3); };
                    VisitPowerTelemetry(p, collect, collectBool);
                }
                else if (!tele)
                {
                    markPowerUnavailable(static_cast<std::uint32_t>(currentAdapter), false, true);
                }
                else
                {
                    markPowerUnavailable(static_cast<std::uint32_t>(currentAdapter), true, false);
                }
                log << "  Adapter " << i << " ctlPowerTelemetryGetV2: " << ResultName(sr) << " (" << Hex(sr) << ")\n";
                if(sr==kSuccess) {
                    VisitPowerTelemetry(p,
                        [&](const std::string& name, const Item& item) { log<<"    "<<name<<" supported="<<(item.supported?"true":"false")<<" type="<<(unsigned)item.type<<" units="<<item.units<<" raw_u64="<<item.value.u64<<" raw_double="<<item.value.d<<" decoded="<<DecodeItem(item)<<"\n"; },
                        [&](const std::string& name, bool value) { log<<"    "<<name<<" raw_bool="<<(value?"true":"false")<<"\n"; });
                }
            }
            log.flush();
            nextSample += samplingCadence;
            if(sample<20)
            {
                const auto now=std::chrono::steady_clock::now();
                if(now<nextSample) std::this_thread::sleep_until(nextSample);
                else log<<"Sampling deadline overrun_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(now-nextSample).count()<<"\n";
            }
        }
        if (!tele)
        {
            for (std::uint32_t i=0;i<count;++i) markPowerUnavailable(i, false, true);
        }
        success=!stop_;
    } catch(...) { }
    if(api&&library){if(auto close=Get<CloseFn>(library,"ctlClose"))close(api);api=nullptr;}if(library)FreeLibrary(library);if(log.is_open()){log<<"\n=== SUMMARY ===\n";for(const auto& [name,series]:metrics){const auto verdict=ClassifyIgclSamples(series);log<<name<<"\n  Verdict: "<<IgclDiagnosticClassName(verdict)<<"\n  Samples: "<<series.values.size()<<"\n  Min: "<<IgclSampleMinimum(series)<<"\n  Max: "<<IgclSampleMaximum(series)<<"\n  Raw samples preserved: "<<series.rawValues.size()<<"\n";}log<<"SKIPPED_MUTATION_CAPABLE: mutation-capable setters/reset/configuration APIs were never dispatched\n";log.flush();}if(success) { PlayDiagnosticCompletionSound(); Status(L"Completed"); } else if(!stop_)Status(L"Failed"); if(notifyWindow_) PostMessageW(notifyWindow_,kIgclDiagnosticCompleted,success?1:0,0);running_=false;
}
}
