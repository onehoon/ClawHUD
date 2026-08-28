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

constexpr Result kSuccess = 0;
constexpr std::uint32_t kUseLevelZero = 1;
constexpr std::uint32_t kApiVersion = (1u << 16) | 1u;
constexpr std::uint32_t kLiveState = 19;
constexpr std::uint32_t kCustom = 5;

using InitArgs = igcl::InitArgs; using LiveState = igcl::LiveState; using FeatureRequest = igcl::FeatureRequest;
using Item = igcl::Item; using PowerV2 = igcl::PowerTelemetryV2;
struct DeviceProps { std::uint32_t size{}; std::uint8_t version{}; void* deviceId{}; std::uint32_t deviceIdSize{}; std::uint32_t deviceType{}; std::uint64_t supported{}; std::uint64_t driverVersion{}; std::uint8_t firmware[32]{}; std::uint32_t vendor{}, device{}, revision{}, eus{}, subslices{}, slices{}; char name[100]{}; std::uint64_t graphicsFlags{}; std::uint32_t frequency{}; std::uint16_t subsys{}, subsysVendor{}; std::uint8_t bdf[3]{}; std::uint8_t bdfPad{}; std::uint32_t xeCores{}; std::uint8_t reserved[108]{}; };
struct PciProps { std::uint32_t size{}; std::uint8_t version{}; std::uint8_t data[96]{}; };
struct PciState { std::uint32_t size{}; std::uint8_t version{}; std::uint8_t data[64]{}; };
struct FreqProps { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t type{}; bool canControl{}; double min{}, max{}; };
struct FreqState { std::uint32_t size{}; std::uint8_t version{}; double voltage{}, request{}, tdp{}, efficient{}, actual{}; std::uint32_t throttle{}; };
struct EngineProps { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t type{}; };
struct EngineStats { std::uint32_t size{}; std::uint8_t version{}; std::uint64_t active{}, timestamp{}; };
struct MemProps { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t type{}; std::uint64_t physical{}; };
struct MemState { std::uint32_t size{}; std::uint8_t version{}; std::uint64_t free{}, sizeBytes{}; };
struct TempProps { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t type{}; double max{}; };
struct TempState { std::uint32_t size{}; std::uint8_t version{}; double current{}; };
struct PowerProps { std::uint32_t size{}; std::uint8_t version{}; bool canControl{}; std::int32_t def{}, min{}, max{}; };
struct Energy { std::uint32_t size{}; std::uint8_t version{}; std::uint64_t energy{}, timestamp{}; };
struct FanProps { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t modes{}, units{}; };

template<class T> T Get(HMODULE m, const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
std::string ResultName(Result r) { switch(r) { case 0:return "CTL_RESULT_SUCCESS"; case 0x40000007:return "CTL_RESULT_ERROR_NOT_AVAILABLE"; case 0x4000000a:return "CTL_RESULT_ERROR_UNSUPPORTED_FEATURE"; case 0x40000009:return "CTL_RESULT_ERROR_UNSUPPORTED_VERSION"; case 0x4000000b:return "CTL_RESULT_ERROR_INVALID_ARGUMENT"; default:return "UNKNOWN"; } }
std::string Hex(Result r) { std::ostringstream s; s<<"0x"<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill('0')<<r; return s.str(); }
std::wstring Now(bool file=false) { auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm tm{}; localtime_s(&tm,&t); std::wstringstream s; s<<std::put_time(&tm,file?L"%Y%m%d-%H%M%S":L"%Y-%m-%d %H:%M:%S"); return s.str(); }
std::string Narrow(const std::wstring& v) { if(v.empty())return{}; int n=WideCharToMultiByte(CP_UTF8,0,v.data(),(int)v.size(),nullptr,0,nullptr,nullptr); std::string s(n,'\0'); WideCharToMultiByte(CP_UTF8,0,v.data(),(int)v.size(),s.data(),n,nullptr,nullptr); return s; }
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
        else if (std::string(title) == "MEMORY") { igcl::MemoryProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);if(r==0)o<<"MemoryType: "<<p.type<<"\nLocation: "<<p.location<<"\nPhysicalBytes: "<<p.physicalSize<<"\nBusWidth: "<<p.busWidth<<"\nChannels: "<<p.numChannels<<"\n";}igcl::MemoryState s{};s.Size=sizeof(s);auto g=Get<igcl::StateFn>(lib,stateName);if(g){r=g(handles[i],&s);LogCall(o,stateName,r);if(r==0)o<<"FreeBytes: "<<s.free<<"\nTotalBytes: "<<s.size<<"\n";}if(auto b=Get<igcl::StateFn>(lib,"ctlMemoryGetBandwidth")){igcl::MemoryBandwidth bw{};bw.Size=sizeof(bw);r=b(handles[i],&bw);LogCall(o,"ctlMemoryGetBandwidth",r);if(r==0)o<<"MaxBandwidth: "<<bw.maxBandwidth<<"\nReadCounter: "<<bw.readCounter<<"\nWriteCounter: "<<bw.writeCounter<<"\nTimestampUs: "<<bw.timestamp<<"\n";} }
        else if (std::string(title) == "TEMPERATURE") { igcl::TemperatureProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);o<<"SensorType: "<<p.type<<"\nMaxC: "<<p.maxTemperature<<"\n";}auto g=reinterpret_cast<Result(__cdecl*)(void*,double*)>(GetProcAddress(lib,stateName));if(g){double temperature=0;r=g(handles[i],&temperature);LogCall(o,stateName,r);if(r==0)o<<"CurrentC: "<<temperature<<"\n";} }
        else if (std::string(title) == "POWER") { igcl::PowerProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);o<<"CanControl: "<<(p.canControl?"true":"false")<<"\nDefaultLimitMilliwatts: "<<p.defaultLimit<<"\nMinLimitMilliwatts: "<<p.minLimit<<"\nMaxLimitMilliwatts: "<<p.maxLimit<<"\n";}igcl::PowerEnergy e{};e.Size=sizeof(e);auto g=Get<igcl::StateFn>(lib,stateName);if(g){r=g(handles[i],&e);LogCall(o,stateName,r);if(r==0)o<<"EnergyMicrojoules: "<<e.energy<<"\nTimestampUs: "<<e.timestamp<<"\n";} }
        else if (std::string(title) == "FAN") { igcl::FanProperties p{};p.Size=sizeof(p);auto f=Get<igcl::PropsFn>(lib,propsName);if(f){r=f(handles[i],&p);LogCall(o,propsName,r);o<<"CanControl: "<<(p.canControl?"true":"false")<<"\nSupportedModes: "<<p.supportedModes<<"\nSupportedUnits: "<<p.supportedUnits<<"\nMaxRPM: "<<p.maxRPM<<"\n";}auto g=Get<igcl::FanStateFn>(lib,"ctlFanGetState");if(g){std::int32_t speed=-1;r=g(handles[i],0,&speed);LogCall(o,"ctlFanGetState",r);o<<"SpeedRaw: "<<speed<<"\n";} }
        else o << "Read-only output handle enumerated; typed display properties are unavailable in this pinned subset.\n";
    }
}
}

const char* IgclDiagnosticClassName(IgclDiagnosticClass v) noexcept { switch(v){case IgclDiagnosticClass::SupportedActive:return "SUPPORTED_ACTIVE";case IgclDiagnosticClass::SupportedZero:return "SUPPORTED_ZERO";case IgclDiagnosticClass::SupportedConstant:return "SUPPORTED_CONSTANT";case IgclDiagnosticClass::Unsupported:return "UNSUPPORTED";case IgclDiagnosticClass::NoDomain:return "NO_DOMAIN";case IgclDiagnosticClass::SymbolMissing:return "SYMBOL_MISSING";case IgclDiagnosticClass::ApiError:return "API_ERROR";default:return "SKIPPED_MUTATION_CAPABLE";} }
IgclDiagnosticClass ClassifyIgclSamples(const IgclSampleSeries& s) noexcept { if(!s.hasDomain)return IgclDiagnosticClass::NoDomain; if(!s.apiSucceeded)return IgclDiagnosticClass::ApiError; if(!s.supported)return IgclDiagnosticClass::Unsupported; if(s.values.empty())return IgclDiagnosticClass::SupportedZero; const auto [a,b]=std::minmax_element(s.values.begin(),s.values.end()); if(*a==0.0&&*b==0.0)return IgclDiagnosticClass::SupportedZero; return *a==*b?IgclDiagnosticClass::SupportedConstant:IgclDiagnosticClass::SupportedActive; }
double IgclSampleMinimum(const IgclSampleSeries& s) noexcept { return s.values.empty()?0.0:*std::min_element(s.values.begin(),s.values.end()); }
double IgclSampleMaximum(const IgclSampleSeries& s) noexcept { return s.values.empty()?0.0:*std::max_element(s.values.begin(),s.values.end()); }

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
        auto en=Get<EnumDevicesFn>(library,"ctlEnumerateDevices"); if(!en){log<<"ctlEnumerateDevices: SYMBOL_MISSING\n";throw std::runtime_error("enum symbol");} std::uint32_t count=0;r=en(api,&count,nullptr);LogCall(log,"ctlEnumerateDevices(count)",r);if(r!=0||!count){log<<"Adapters: NO_DOMAIN\n";success=true;throw std::runtime_error("no adapters");}std::vector<Device> ds(count);r=en(api,&count,ds.data());LogCall(log,"ctlEnumerateDevices(handles)",r);if(r!=0)throw std::runtime_error("enum");
        auto devProps=Get<DevicePropsFn>(library,"ctlGetDeviceProperties");auto pci=Get<PciFn>(library,"ctlPciGetProperties");auto tele=Get<TelemetryFn>(library,"ctlPowerTelemetryGetV2");
        for(std::uint32_t i=0;i<count;++i){log<<"\n=== ADAPTER "<<i<<" ===\n";if(devProps){DeviceProps p{};p.size=sizeof(p);r=devProps(ds[i],&p);LogCall(log,"ctlGetDeviceProperties",r);if(r==0)log<<"Device: "<<p.name<<"\nVendor ID: 0x"<<std::hex<<p.vendor<<" Device ID: 0x"<<p.device<<" Revision: 0x"<<p.revision<<"\nDevice Type: "<<std::dec<<p.deviceType<<"\nDriver Version Raw: "<<p.driverVersion<<"\nGraphics Capability Flags Raw: 0x"<<std::hex<<p.graphicsFlags<<"\n";}else log<<"ctlGetDeviceProperties: SYMBOL_MISSING\n";if(pci){PciProps p{};p.size=sizeof(p);r=pci(ds[i],&p);LogCall(log,"ctlPciGetProperties",r);log<<"PCI raw bytes retained: "<<sizeof(p.data)<<"\n";}else log<<"ctlPciGetProperties: SYMBOL_MISSING\n";log<<"\n=== POWER TELEMETRY V2 ===\n";if(tele){PowerV2 p{};p.size=sizeof(p);r=tele(ds[i],&p);LogCall(log,"ctlPowerTelemetryGetV2",r);const char* names[]={"timeStamp","gpuEnergyCounter","gpuVoltage","gpuCurrentClockFrequency","gpuCurrentTemperature","globalActivityCounter","renderComputeActivityCounter","mediaActivityCounter","vramEnergyCounter","vramVoltage","vramCurrentClockFrequency","vramCurrentEffectiveFrequency","vramReadBandwidthCounter","vramWriteBandwidthCounter","vramCurrentTemperature","totalCardEnergyCounter","gpuVrTemp","vramVrTemp","saVrTemp","gpuEffectiveClock","gpuOverVoltagePercent","gpuPowerPercent","gpuTemperaturePercent","vramReadBandwidth","vramWriteBandwidth"};Item* items[]={&p.timeStamp,&p.gpuEnergyCounter,&p.gpuVoltage,&p.gpuCurrentClockFrequency,&p.gpuCurrentTemperature,&p.globalActivityCounter,&p.renderComputeActivityCounter,&p.mediaActivityCounter,&p.vramEnergyCounter,&p.vramVoltage,&p.vramCurrentClockFrequency,&p.vramCurrentEffectiveFrequency,&p.vramReadBandwidthCounter,&p.vramWriteBandwidthCounter,&p.vramCurrentTemperature,&p.totalCardEnergyCounter,&p.gpuVrTemp,&p.vramVrTemp,&p.saVrTemp,&p.gpuEffectiveClock,&p.gpuOverVoltagePercent,&p.gpuPowerPercent,&p.gpuTemperaturePercent,&p.vramReadBandwidth,&p.vramWriteBandwidth};for(size_t j=0;j<std::size(names);++j)log<<names[j]<<"\n  Supported: "<<(items[j]->supported?"true":"false")<<"\n  Type: "<<(unsigned)items[j]->type<<"\n  Units: "<<items[j]->units<<"\n  Raw uint64: "<<items[j]->value.u64<<"\n  Raw double: "<<items[j]->value.d<<"\n";}else log<<"ctlPowerTelemetryGetV2: SYMBOL_MISSING\n";
            log<<"\n=== SAFE SURFACE SYMBOL INVENTORY ===\n";const char* syms[]={"ctlEnumFrequencyDomains","ctlFrequencyGetProperties","ctlFrequencyGetState","ctlEnumEngineGroups","ctlEngineGetProperties","ctlEngineGetActivity","ctlEnumMemoryModules","ctlMemoryGetProperties","ctlMemoryGetState","ctlMemoryGetBandwidth","ctlEnumTemperatureSensors","ctlTemperatureGetProperties","ctlTemperatureGetState","ctlEnumPowerDomains","ctlPowerGetProperties","ctlPowerGetEnergyCounter","ctlEnumFans","ctlFanGetProperties","ctlFanGetState","ctlEnumerateDisplayOutputs","ctlGetDisplayProperties","ctlGetSet3DFeature"};for(auto s:syms)log<<s<<": "<<(Get<void*>(library,s)?"PRESENT_READ_ONLY_OR_MIXED":"SYMBOL_MISSING")<<"\n";log<<"Mutation-capable setters are never dispatched: SKIPPED_MUTATION_CAPABLE\n";}
        for(std::uint32_t i=0;i<count;++i)
        {
            log << "\n=== ADAPTER " << i << " READ-ONLY DOMAINS ===\n";
            log << "\n=== 3D LIVE STATE ===\n";
            if (const auto live = Get<GetSet3DFn>(library, "ctlGetSet3DFeature"); live)
            {
                LiveState state{}; FeatureRequest request{}; request.size=sizeof(request); request.featureType=19; request.valueType=5; request.customValueSize=sizeof(state); request.customValue=&state;
                std::string appName=""; if(!path.empty()) appName=std::filesystem::path(path).filename().string(); request.applicationName=appName.empty()?nullptr:appName.data(); request.applicationNameLength=static_cast<std::int8_t>(appName.size());
                const Result lr=live(ds[i],&request); LogCall(log,"ctlGetSet3DFeature (read-only)",lr); log<<"Graphics API mask raw: 0x"<<std::hex<<state.gfxApi<<"\nTarget FPS raw: "<<std::dec<<state.targetFps<<"\nFrame pacing raw: "<<state.framePacingStatus<<"\n";
            }
            else log << "ctlGetSet3DFeature: SYMBOL_MISSING\n";
            ProbeCategory(log,library,ds[i],"FREQUENCY","ctlEnumFrequencyDomains","ctlFrequencyGetProperties","ctlFrequencyGetState");
            ProbeCategory(log,library,ds[i],"ENGINE","ctlEnumEngineGroups","ctlEngineGetProperties","ctlEngineGetActivity");
            ProbeCategory(log,library,ds[i],"MEMORY","ctlEnumMemoryModules","ctlMemoryGetProperties","ctlMemoryGetState");
            ProbeCategory(log,library,ds[i],"TEMPERATURE","ctlEnumTemperatureSensors","ctlTemperatureGetProperties","ctlTemperatureGetState");
            ProbeCategory(log,library,ds[i],"POWER","ctlEnumPowerDomains","ctlPowerGetProperties","ctlPowerGetEnergyCounter");
            ProbeCategory(log,library,ds[i],"FAN","ctlEnumFans","ctlFanGetProperties",nullptr);
            ProbeCategory(log,library,ds[i],"DISPLAY / OUTPUT","ctlEnumerateDisplayOutputs","ctlGetDisplayProperties",nullptr);
        }
        log<<"\n=== SAMPLING ===\nCadence: 250 ms\nTarget samples: 20\nRaw samples retain support/type/unit/value fields above. Counters are not converted to instantaneous values.\n";
        auto collect = [&](const char* name, const Item& item, bool counter=false) { auto& s=metrics[name]; s.supported=item.supported; s.values.push_back(item.value.d); s.rawValues.push_back(item.value.u64); s.timestamps.push_back(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())); (void)counter; };
        if (tele) for (int sample=1; sample<=20 && !stop_; ++sample)
        {
            log << "Sample " << std::setw(2) << std::setfill('0') << sample << " timestamp_ms="
                << (sample-1)*250 << "\n";
            for (std::uint32_t i=0;i<count;++i)
            {
                PowerV2 p{}; p.size=sizeof(p); const Result sr=tele(ds[i],&p);
                if (sr == kSuccess) { collect("gpuCurrentClockFrequency",p.gpuCurrentClockFrequency); collect("gpuCurrentTemperature",p.gpuCurrentTemperature); collect("gpuEnergyCounter",p.gpuEnergyCounter,true); collect("globalActivityCounter",p.globalActivityCounter,true); collect("renderComputeActivityCounter",p.renderComputeActivityCounter,true); collect("mediaActivityCounter",p.mediaActivityCounter,true); collect("vramEnergyCounter",p.vramEnergyCounter,true); collect("vramCurrentClockFrequency",p.vramCurrentClockFrequency); collect("vramCurrentEffectiveFrequency",p.vramCurrentEffectiveFrequency); collect("vramReadBandwidthCounter",p.vramReadBandwidthCounter,true); collect("vramWriteBandwidthCounter",p.vramWriteBandwidthCounter,true); collect("vramCurrentTemperature",p.vramCurrentTemperature); collect("totalCardEnergyCounter",p.totalCardEnergyCounter,true); collect("gpuEffectiveClock",p.gpuEffectiveClock); collect("gpuPowerPercent",p.gpuPowerPercent); collect("vramReadBandwidth",p.vramReadBandwidth); collect("vramWriteBandwidth",p.vramWriteBandwidth); }
                log << "  Adapter " << i << " ctlPowerTelemetryGetV2: " << ResultName(sr) << " (" << Hex(sr) << ")\n";
                if(sr==kSuccess) { const Item* items[]={&p.timeStamp,&p.gpuEnergyCounter,&p.gpuCurrentClockFrequency,&p.gpuCurrentTemperature,&p.globalActivityCounter,&p.renderComputeActivityCounter,&p.mediaActivityCounter,&p.vramEnergyCounter,&p.vramCurrentClockFrequency,&p.vramCurrentEffectiveFrequency,&p.vramReadBandwidthCounter,&p.vramWriteBandwidthCounter,&p.vramCurrentTemperature,&p.totalCardEnergyCounter,&p.gpuEffectiveClock,&p.gpuPowerPercent}; for(size_t j=0;j<std::size(items);++j) log<<"    field["<<j<<"] supported="<<(items[j]->supported?"true":"false")<<" type="<<(unsigned)items[j]->type<<" units="<<items[j]->units<<" raw_u64="<<items[j]->value.u64<<" raw_double="<<items[j]->value.d<<"\n"; }
            }
            log.flush(); if(sample<20) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        success=!stop_;
    } catch(...) { }
    if(api&&library){if(auto close=Get<CloseFn>(library,"ctlClose"))close(api);api=nullptr;}if(library)FreeLibrary(library);if(log.is_open()){log<<"\n=== SUMMARY ===\n";for(const auto& [name,series]:metrics){const auto verdict=ClassifyIgclSamples(series);log<<name<<"\n  Verdict: "<<IgclDiagnosticClassName(verdict)<<"\n  Samples: "<<series.values.size()<<"\n  Min: "<<IgclSampleMinimum(series)<<"\n  Max: "<<IgclSampleMaximum(series)<<"\n  Raw samples preserved: "<<series.rawValues.size()<<"\n";}log<<"SKIPPED_MUTATION_CAPABLE: mutation-capable setters/reset/configuration APIs were never dispatched\n";log.flush();}if(success) { PlayDiagnosticCompletionSound(); Status(L"Completed"); } else if(!stop_)Status(L"Failed"); if(notifyWindow_) PostMessageW(notifyWindow_,kIgclDiagnosticCompleted,success?1:0,0);running_=false;
}
}
