#include "PresentMonApi2Client.h"

#include <windows.h>

#include <utility>

namespace clawhud
{
namespace
{
using ApiGetVersion = PM_STATUS(__cdecl*)(PM_VERSION*);
using ApiOpenSession = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE*);
using ApiCloseSession = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE);
using ApiStartTracking = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, std::uint32_t);
using ApiStopTracking = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, std::uint32_t);
using ApiGetRoot = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, const PM_INTROSPECTION_ROOT**);
using ApiFreeRoot = PM_STATUS(__cdecl*)(const PM_INTROSPECTION_ROOT*);
using ApiSetPeriod = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, std::uint32_t, std::uint32_t);
using ApiSetFlush = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, std::uint32_t);
using ApiFlushFrames = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, std::uint32_t);
using ApiRegisterDynamic = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, PM_DYNAMIC_QUERY_HANDLE*,
    PM_QUERY_ELEMENT*, std::uint64_t, double, double);
using ApiFreeDynamic = PM_STATUS(__cdecl*)(PM_DYNAMIC_QUERY_HANDLE);
using ApiPollDynamic = PM_STATUS(__cdecl*)(PM_DYNAMIC_QUERY_HANDLE, std::uint32_t,
    std::uint8_t*, std::uint32_t*);
using ApiPollStatic = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, const PM_QUERY_ELEMENT*,
    std::uint32_t, std::uint8_t*);
using ApiRegisterFrame = PM_STATUS(__cdecl*)(PM_SESSION_HANDLE, PM_FRAME_QUERY_HANDLE*,
    PM_QUERY_ELEMENT*, std::uint64_t, std::uint32_t*);
using ApiConsumeFrames = PM_STATUS(__cdecl*)(PM_FRAME_QUERY_HANDLE, std::uint32_t,
    std::uint8_t*, std::uint32_t*);
using ApiFreeFrame = PM_STATUS(__cdecl*)(PM_FRAME_QUERY_HANDLE);

template<class F> F Resolve(HMODULE module, const char* name)
{
    return reinterpret_cast<F>(GetProcAddress(module, name));
}
}

struct PresentMonApi2Client::Endpoints
{
    ApiGetVersion getVersion{};
    ApiOpenSession openSession{};
    ApiCloseSession closeSession{};
    ApiStartTracking startTracking{};
    ApiStopTracking stopTracking{};
    ApiGetRoot getRoot{};
    ApiFreeRoot freeRoot{};
    ApiSetPeriod setPeriod{};
    ApiSetFlush setFlush{};
    ApiFlushFrames flushFrames{};
    ApiRegisterDynamic registerDynamic{};
    ApiFreeDynamic freeDynamic{};
    ApiPollDynamic pollDynamic{};
    ApiPollStatic pollStatic{};
    ApiRegisterFrame registerFrame{};
    ApiConsumeFrames consumeFrames{};
    ApiFreeFrame freeFrame{};

    bool Complete() const noexcept
    {
        return getVersion && openSession && closeSession && startTracking &&
            stopTracking && getRoot && freeRoot && setPeriod && setFlush &&
            flushFrames && registerDynamic && freeDynamic && pollDynamic &&
            pollStatic && registerFrame && consumeFrames && freeFrame;
    }

    const char* FirstMissingName() const noexcept
    {
        if (!getVersion) return "pmGetApiVersion";
        if (!openSession) return "pmOpenSession";
        if (!closeSession) return "pmCloseSession";
        if (!startTracking) return "pmStartTrackingProcess";
        if (!stopTracking) return "pmStopTrackingProcess";
        if (!getRoot) return "pmGetIntrospectionRoot";
        if (!freeRoot) return "pmFreeIntrospectionRoot";
        if (!setPeriod) return "pmSetTelemetryPollingPeriod";
        if (!setFlush) return "pmSetEtwFlushPeriod";
        if (!flushFrames) return "pmFlushFrames";
        if (!registerDynamic) return "pmRegisterDynamicQuery";
        if (!freeDynamic) return "pmFreeDynamicQuery";
        if (!pollDynamic) return "pmPollDynamicQuery";
        if (!pollStatic) return "pmPollStaticQuery";
        if (!registerFrame) return "pmRegisterFrameQuery";
        if (!consumeFrames) return "pmConsumeFrames";
        if (!freeFrame) return "pmFreeFrameQuery";
        return nullptr;
    }
};

const char* PresentMonApi2InitFailureName(PresentMonApi2InitFailure failure) noexcept
{
    switch (failure)
    {
    case PresentMonApi2InitFailure::None: return "NONE";
    case PresentMonApi2InitFailure::LoaderNotFound: return "LOADER_NOT_FOUND";
    case PresentMonApi2InitFailure::LoaderLoadFailed: return "LOADER_LOAD_FAILED";
    case PresentMonApi2InitFailure::MissingEndpoint: return "MISSING_ENDPOINT";
    case PresentMonApi2InitFailure::VersionQueryFailed: return "VERSION_QUERY_FAILED";
    default: return "UNKNOWN";
    }
}

std::filesystem::path PresentMonApi2AppLocalLoaderPath(
    const std::filesystem::path& modulePath)
{
    if (modulePath.empty()) return {};
    const auto loader = modulePath.parent_path() / L"PresentMonAPI2Loader.dll";
    return std::filesystem::exists(loader) ? loader : std::filesystem::path{};
}

PresentMonApi2Client::~PresentMonApi2Client()
{
    Shutdown();
}

bool PresentMonApi2Client::Initialize()
{
    Shutdown();
    initStatus_ = {};
    wchar_t moduleName[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, moduleName, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        loaderError_ = GetLastError();
        initStatus_.failure = PresentMonApi2InitFailure::LoaderLoadFailed;
        initStatus_.win32Error = loaderError_;
        return false;
    }
    loaderPath_ = PresentMonApi2AppLocalLoaderPath(moduleName);
    if (loaderPath_.empty())
    {
        loaderError_ = ERROR_FILE_NOT_FOUND;
        initStatus_.failure = PresentMonApi2InitFailure::LoaderNotFound;
        initStatus_.win32Error = loaderError_;
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    loader_ = LoadLibraryExW(loaderPath_.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    loaderError_ = loader_ ? ERROR_SUCCESS : GetLastError();
    if (!loader_)
    {
        initStatus_.failure = PresentMonApi2InitFailure::LoaderLoadFailed;
        initStatus_.win32Error = loaderError_;
        return false;
    }

    endpoints_ = new Endpoints{
        Resolve<ApiGetVersion>(loader_, "pmGetApiVersion"),
        Resolve<ApiOpenSession>(loader_, "pmOpenSession"),
        Resolve<ApiCloseSession>(loader_, "pmCloseSession"),
        Resolve<ApiStartTracking>(loader_, "pmStartTrackingProcess"),
        Resolve<ApiStopTracking>(loader_, "pmStopTrackingProcess"),
        Resolve<ApiGetRoot>(loader_, "pmGetIntrospectionRoot"),
        Resolve<ApiFreeRoot>(loader_, "pmFreeIntrospectionRoot"),
        Resolve<ApiSetPeriod>(loader_, "pmSetTelemetryPollingPeriod"),
        Resolve<ApiSetFlush>(loader_, "pmSetEtwFlushPeriod"),
        Resolve<ApiFlushFrames>(loader_, "pmFlushFrames"),
        Resolve<ApiRegisterDynamic>(loader_, "pmRegisterDynamicQuery"),
        Resolve<ApiFreeDynamic>(loader_, "pmFreeDynamicQuery"),
        Resolve<ApiPollDynamic>(loader_, "pmPollDynamicQuery"),
        Resolve<ApiPollStatic>(loader_, "pmPollStaticQuery"),
        Resolve<ApiRegisterFrame>(loader_, "pmRegisterFrameQuery"),
        Resolve<ApiConsumeFrames>(loader_, "pmConsumeFrames"),
        Resolve<ApiFreeFrame>(loader_, "pmFreeFrameQuery")};
    if (!endpoints_->Complete())
    {
        initStatus_.failure = PresentMonApi2InitFailure::MissingEndpoint;
        initStatus_.missingEndpoint = endpoints_->FirstMissingName();
        Shutdown();
        return false;
    }
    const PM_STATUS versionStatus = endpoints_->getVersion(&version_);
    if (versionStatus != PM_STATUS_SUCCESS)
    {
        initStatus_.failure = PresentMonApi2InitFailure::VersionQueryFailed;
        initStatus_.apiStatus = versionStatus;
        Shutdown();
        return false;
    }
    initialized_ = true;
    return true;
}

void PresentMonApi2Client::Shutdown() noexcept
{
    CloseSession();
    delete endpoints_;
    endpoints_ = nullptr;
    if (loader_) FreeLibrary(loader_);
    loader_ = nullptr;
    initialized_ = false;
    version_ = {};
}

void PresentMonApi2Client::CloseSession() noexcept
{
    if (session_ && endpoints_ && endpoints_->closeSession)
        endpoints_->closeSession(session_);
    session_ = nullptr;
}

PM_STATUS PresentMonApi2Client::OpenSession()
{ return endpoints_ && endpoints_->openSession ? endpoints_->openSession(&session_) : PM_STATUS_FAILURE; }
PM_STATUS PresentMonApi2Client::StartTrackingProcess(std::uint32_t pid)
{ return session_ && endpoints_ ? endpoints_->startTracking(session_, pid) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::StopTrackingProcess(std::uint32_t pid)
{ return session_ && endpoints_ ? endpoints_->stopTracking(session_, pid) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::GetIntrospectionRoot(const PM_INTROSPECTION_ROOT** root)
{ return session_ && endpoints_ ? endpoints_->getRoot(session_, root) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::FreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* root)
{ return endpoints_ && endpoints_->freeRoot ? endpoints_->freeRoot(root) : PM_STATUS_FAILURE; }
PM_STATUS PresentMonApi2Client::SetTelemetryPollingPeriod(std::uint32_t reserved, std::uint32_t period)
{ return session_ && endpoints_ ? endpoints_->setPeriod(session_, reserved, period) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::SetEtwFlushPeriod(std::uint32_t period)
{ return session_ && endpoints_ ? endpoints_->setFlush(session_, period) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::FlushFrames(std::uint32_t pid)
{ return session_ && endpoints_ ? endpoints_->flushFrames(session_, pid) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::RegisterDynamicQuery(PM_DYNAMIC_QUERY_HANDLE* query, PM_QUERY_ELEMENT* elements,
    std::uint64_t count, double window, double offset)
{ return session_ && endpoints_ ? endpoints_->registerDynamic(session_, query, elements, count, window, offset) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::FreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query)
{ return endpoints_ && endpoints_->freeDynamic ? endpoints_->freeDynamic(query) : PM_STATUS_FAILURE; }
PM_STATUS PresentMonApi2Client::PollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query, std::uint32_t pid,
    std::uint8_t* blob, std::uint32_t* swaps)
{ return endpoints_ && endpoints_->pollDynamic ? endpoints_->pollDynamic(query, pid, blob, swaps) : PM_STATUS_FAILURE; }
PM_STATUS PresentMonApi2Client::PollStaticQuery(const PM_QUERY_ELEMENT* element, std::uint32_t pid, std::uint8_t* blob)
{ return session_ && endpoints_ ? endpoints_->pollStatic(session_, element, pid, blob) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::RegisterFrameQuery(PM_FRAME_QUERY_HANDLE* query, PM_QUERY_ELEMENT* elements,
    std::uint64_t count, std::uint32_t* blobSize)
{ return session_ && endpoints_ ? endpoints_->registerFrame(session_, query, elements, count, blobSize) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS PresentMonApi2Client::ConsumeFrames(PM_FRAME_QUERY_HANDLE query, std::uint32_t pid,
    std::uint8_t* blob, std::uint32_t* count)
{ return endpoints_ && endpoints_->consumeFrames ? endpoints_->consumeFrames(query, pid, blob, count) : PM_STATUS_FAILURE; }
PM_STATUS PresentMonApi2Client::FreeFrameQuery(PM_FRAME_QUERY_HANDLE query)
{ return endpoints_ && endpoints_->freeFrame ? endpoints_->freeFrame(query) : PM_STATUS_FAILURE; }
}
